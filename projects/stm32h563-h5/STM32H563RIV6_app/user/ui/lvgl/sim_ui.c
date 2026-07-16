/**
 * @file sim_ui.c
 * @brief Product-oriented 480x320 LVGL interface backed by real App services.
 *
 * Ownership boundary:
 * - This module owns LVGL objects and short-lived input text only.
 * - It reads coherent data through app_ui_model and requests actions through
 *   App/BSP public APIs; it never sends W800 AT commands or parses protocols.
 * - Only the current content page is instantiated, keeping the 160 KiB LVGL
 *   heap bounded. The common header and bottom navigation remain resident.
 */

#include "sim_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_ui_model.h"
#include "app_w800.h"
#include "app_board_io.h"
#include "app_self_test.h"
#include "bsp.h"
#include "bsp_pwm.h"
#include "lvgl.h"

extern const lv_image_dsc_t boot_eye_img;

#define UI_W                         480
#define UI_H                         320
#define UI_HEADER_H                   38
#define UI_NAV_H                      48
#define UI_CONTENT_H  (UI_H - UI_HEADER_H - UI_NAV_H)
#define UI_BOOT_EYE_Y                160
#define UI_BACKLIGHT_HZ            20000U

#define COLOR_BG          lv_color_hex(0x07162F)
#define COLOR_HEADER      lv_color_hex(0x0B2147)
#define COLOR_PANEL       lv_color_hex(0x102D5A)
#define COLOR_PANEL_ALT   lv_color_hex(0x153A70)
#define COLOR_BLUE        lv_color_hex(0x31A8FF)
#define COLOR_BLUE_DARK   lv_color_hex(0x1668B7)
#define COLOR_TEXT        lv_color_hex(0xF3F7FF)
#define COLOR_MUTED       lv_color_hex(0x9EB8DA)
#define COLOR_GREEN       lv_color_hex(0x75E887)
#define COLOR_AMBER       lv_color_hex(0xFFC857)
#define COLOR_RED         lv_color_hex(0xFF6B74)
#define COLOR_BLACK       lv_color_hex(0x000000)

typedef enum
{
    VIEW_HOME = 0,
    VIEW_NETWORK,
    VIEW_DIAGNOSTICS,
    VIEW_SETTINGS,
    VIEW_WIFI_CONFIG,
    VIEW_MODBUS,
    VIEW_CAN,
    VIEW_BOARD_TEST,
    VIEW_USB,
    VIEW_BRIGHTNESS,
    VIEW_TIME,
    VIEW_DEVICE,
    VIEW_FIRMWARE
} ui_view_t;

typedef enum
{
    WIFI_STAGE_SCAN_LIST = 0,
    WIFI_STAGE_CREDENTIAL_FORM,
    WIFI_STAGE_CONNECTING
} wifi_stage_t;

typedef struct
{
    lv_obj_t *home_wifi;
    lv_obj_t *home_mqtt;
    lv_obj_t *home_modbus;
    lv_obj_t *home_health;
    lv_obj_t *home_link;
    lv_obj_t *home_alert;

    lv_obj_t *net_w800;
    lv_obj_t *net_ble;
    lv_obj_t *net_mqtt;
    lv_obj_t *net_socket;
    lv_obj_t *net_note;

    lv_obj_t *modbus_unit;
    lv_obj_t *modbus_frames;
    lv_obj_t *modbus_errors;
    lv_obj_t *modbus_loopback;
    lv_obj_t *modbus_registers;

    lv_obj_t *can_state;
    lv_obj_t *can_counts;
    lv_obj_t *can_cycles;
    lv_obj_t *can_latency;
    lv_obj_t *can_errors;

    lv_obj_t *board_summary;
    lv_obj_t *board_items[APP_SELF_TEST_ITEM_COUNT];

    lv_obj_t *usb_state;
    lv_obj_t *usb_frames;
    lv_obj_t *usb_transport;
    lv_obj_t *usb_errors;

    lv_obj_t *brightness_value;
    lv_obj_t *time_value;
    lv_obj_t *firmware_ota;
} dynamic_objects_t;

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *shell;
    lv_obj_t *header;
    lv_obj_t *back_button;
    lv_obj_t *title;
    lv_obj_t *status_pill;
    lv_obj_t *status_text;
    lv_obj_t *content;
    lv_obj_t *nav;
    lv_obj_t *nav_button[4];
    lv_obj_t *boot_layer;
    lv_obj_t *boot_top_lid;
    lv_obj_t *boot_bottom_lid;
    lv_obj_t *boot_flash;
    lv_obj_t *wifi_ssid;
    lv_obj_t *wifi_password;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *wifi_result;
    lv_obj_t *wifi_scan_status;
    lv_obj_t *wifi_connection_status;
    dynamic_objects_t dyn;
    app_ui_model_snapshot_t model;
    ui_view_t view;
    ui_view_t back_view;
    wifi_stage_t wifi_stage;
    uint32_t wifi_scan_generation_seen;
    char wifi_selected_ssid[33];
    uint8_t primary_page;
} ui_state_t;

static ui_state_t s_ui;

static void show_view(ui_view_t view);

/** @brief Create a non-scrollable object with a solid background. */
static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y,
                     int32_t width, int32_t height, lv_color_t color,
                     int32_t radius)
{
    lv_obj_t *object = lv_obj_create(parent);

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

/** @brief Create one positioned label with bounded width. */
static lv_obj_t *text_label(lv_obj_t *parent, const char *text,
                            int32_t x, int32_t y, int32_t width,
                            const lv_font_t *font, lv_color_t color,
                            lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

/** @brief Create a touch button and center one label inside it. */
static lv_obj_t *action_button(lv_obj_t *parent, const char *caption,
                               int32_t x, int32_t y,
                               int32_t width, int32_t height,
                               lv_color_t color, const lv_font_t *font,
                               lv_event_cb_t callback, uintptr_t user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *caption_label;

    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_mix(COLOR_BLUE, color, 80), 0);
    lv_obj_set_style_bg_color(button, lv_color_lighten(color, 18), LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if(callback != NULL)
    {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED,
                            (void *)user_data);
    }

    caption_label = lv_label_create(button);
    lv_label_set_text(caption_label, caption);
    lv_obj_set_width(caption_label, width - 8);
    lv_obj_set_style_text_font(caption_label, font, 0);
    lv_obj_set_style_text_color(caption_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_align(caption_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption_label);
    return button;
}

/** @brief Create one two-column status row. */
static lv_obj_t *status_row(lv_obj_t *parent, const char *name,
                            int32_t y, lv_obj_t **value)
{
    lv_obj_t *row = box(parent, 0, y, lv_obj_get_width(parent), 32,
                        COLOR_PANEL, 0);

    (void)text_label(row, name, 12, 7, 180, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    *value = text_label(row, "--", 194, 7, lv_obj_get_width(parent) - 206,
                        &lv_font_cjk_14, COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
    return row;
}

/** @brief Update text and state color on an existing object. */
static void set_colored_text(lv_obj_t *object, const char *text, lv_color_t color)
{
    if(object == NULL)
    {
        return;
    }
    lv_label_set_text(object, text);
    lv_obj_set_style_text_color(object, color, 0);
}

static const char *yes_no(uint8_t state, const char *yes, const char *no)
{
    return state != 0U ? yes : no;
}

static lv_color_t state_color(uint8_t state)
{
    return state != 0U ? COLOR_GREEN : COLOR_AMBER;
}

/** @brief Return the parent primary page for a view. */
static uint8_t primary_for_view(ui_view_t view)
{
    switch(view)
    {
        case VIEW_NETWORK:
        case VIEW_WIFI_CONFIG:
            return 1U;
        case VIEW_DIAGNOSTICS:
        case VIEW_MODBUS:
        case VIEW_CAN:
        case VIEW_BOARD_TEST:
        case VIEW_USB:
            return 2U;
        case VIEW_SETTINGS:
        case VIEW_BRIGHTNESS:
        case VIEW_TIME:
        case VIEW_DEVICE:
        case VIEW_FIRMWARE:
            return 3U;
        default:
            return 0U;
    }
}

static const char *title_for_view(ui_view_t view)
{
    switch(view)
    {
        case VIEW_HOME:        return "系统状态";
        case VIEW_NETWORK:     return "网络";
        case VIEW_DIAGNOSTICS: return "诊断";
        case VIEW_SETTINGS:    return "设置";
        case VIEW_WIFI_CONFIG: return "屏幕配网";
        case VIEW_MODBUS:      return "Modbus / RS485";
        case VIEW_CAN:         return "双路 CAN";
        case VIEW_BOARD_TEST:  return "整板自检";
        case VIEW_USB:         return "USB 通信";
        case VIEW_BRIGHTNESS:  return "屏幕亮度";
        case VIEW_TIME:        return "时间";
        case VIEW_DEVICE:      return "设备信息";
        case VIEW_FIRMWARE:    return "固件信息";
        default:               return "系统";
    }
}

/** @brief Select a view from a button's integer user data. */
static void view_button_cb(lv_event_t *event)
{
    ui_view_t target = (ui_view_t)(uintptr_t)lv_event_get_user_data(event);

    if(target == VIEW_WIFI_CONFIG)
    {
        s_ui.wifi_stage = WIFI_STAGE_SCAN_LIST;
        s_ui.wifi_selected_ssid[0] = '\0';
        (void)app_w800_request_scan();
        app_w800_get_scan_snapshot(&s_ui.model.wifi_scan);
        s_ui.wifi_scan_generation_seen = s_ui.model.wifi_scan.generation;
    }
    show_view(target);
}

/** @brief Return from a secondary page. */
static void back_button_cb(lv_event_t *event)
{
    (void)event;
    if(s_ui.view == VIEW_WIFI_CONFIG &&
       s_ui.wifi_stage == WIFI_STAGE_CREDENTIAL_FORM)
    {
        if(s_ui.wifi_password != NULL)
            lv_textarea_set_text(s_ui.wifi_password, "");
        s_ui.wifi_stage = WIFI_STAGE_SCAN_LIST;
        show_view(VIEW_WIFI_CONFIG);
        return;
    }
    show_view(s_ui.back_view);
}

/** @brief Select one of the four primary pages. */
static void nav_button_cb(lv_event_t *event)
{
    static const ui_view_t primary_views[4] =
    {
        VIEW_HOME, VIEW_NETWORK, VIEW_DIAGNOSTICS, VIEW_SETTINGS
    };
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);

    if(index < 4U)
    {
        show_view(primary_views[index]);
    }
}

/** @brief Queue W800 BLE provisioning without blocking the UI. */
static void ble_provision_cb(lv_event_t *event)
{
    (void)event;
    app_w800_request_ble_provisioning();
    if(s_ui.dyn.net_note != NULL)
    {
        set_colored_text(s_ui.dyn.net_note, "BLE 配网请求已发送", COLOR_GREEN);
    }
}

/** @brief Queue a task-context MQTT reconnect. */
static void mqtt_reconnect_cb(lv_event_t *event)
{
    (void)event;
    app_board_request_mqtt_reconnect();
    if(s_ui.dyn.net_note != NULL)
    {
        set_colored_text(s_ui.dyn.net_note, "MQTT 重连请求已发送", COLOR_GREEN);
    }
}

/** @brief Request a fresh bounded whole-board self-test. */
static void run_self_test_cb(lv_event_t *event)
{
    (void)event;
    app_self_test_request_run();
}

/** @brief Apply a brightness change in task context. */
static void brightness_changed_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    bsp_pwm_config_t config;
    uint16_t value = (uint16_t)lv_slider_get_value(slider);
    char buffer[24];

    config.frequency_hz = UI_BACKLIGHT_HZ;
    config.duty_permille = value;
    if(bsp_pwm_configure(BOARD_PWM_LCD_BACKLIGHT, &config, NULL) == BSP_STATUS_OK)
    {
        (void)bsp_pwm_start(BOARD_PWM_LCD_BACKLIGHT);
    }
    (void)snprintf(buffer, sizeof(buffer), "%u%%", (unsigned)(value / 10U));
    set_colored_text(s_ui.dyn.brightness_value, buffer, COLOR_BLUE);
}

/** @brief Bind the common keyboard to the focused text area. */
static void wifi_field_focused_cb(lv_event_t *event)
{
    lv_obj_t *field = lv_event_get_target_obj(event);

    if(s_ui.wifi_keyboard != NULL)
    {
        lv_keyboard_set_textarea(s_ui.wifi_keyboard, field);
    }
}

/** @brief Validate credentials, then show persistent W800 connection progress. */
static void wifi_submit_cb(lv_event_t *event)
{
    app_w800_credentials_result_t result;
    const char *ssid;
    const char *password;

    (void)event;
    if(s_ui.wifi_ssid == NULL || s_ui.wifi_password == NULL)
    {
        return;
    }

    ssid = lv_textarea_get_text(s_ui.wifi_ssid);
    password = lv_textarea_get_text(s_ui.wifi_password);
    result = app_w800_request_credentials(ssid, password);

    switch(result)
    {
        case APP_W800_CREDENTIALS_ACCEPTED:
            (void)strncpy(s_ui.wifi_selected_ssid, ssid,
                          sizeof(s_ui.wifi_selected_ssid) - 1U);
            s_ui.wifi_selected_ssid[sizeof(s_ui.wifi_selected_ssid) - 1U] = '\0';
            lv_textarea_set_text(s_ui.wifi_password, "");
            s_ui.wifi_stage = WIFI_STAGE_CONNECTING;
            show_view(VIEW_WIFI_CONFIG);
            break;
        case APP_W800_CREDENTIALS_INVALID_SSID:
            set_colored_text(s_ui.wifi_result,
                             "SSID 无效，请检查名称", COLOR_RED);
            break;
        case APP_W800_CREDENTIALS_INVALID_PASSWORD:
            set_colored_text(s_ui.wifi_result,
                             "密码需要 8 到 63 位，内容已保留", COLOR_RED);
            break;
        case APP_W800_CREDENTIALS_BUSY:
            set_colored_text(s_ui.wifi_result,
                             "W800 正在处理上次请求，请稍后重试", COLOR_AMBER);
            break;
        default:
            set_colored_text(s_ui.wifi_result,
                             "W800 尚未就绪，密码未清空", COLOR_AMBER);
            break;
    }
}

static void wifi_scan_refresh_cb(lv_event_t *event)
{
    (void)event;
    (void)app_w800_request_scan();
    app_w800_get_scan_snapshot(&s_ui.model.wifi_scan);
    s_ui.wifi_scan_generation_seen = s_ui.model.wifi_scan.generation;
    show_view(VIEW_WIFI_CONFIG);
}

static void wifi_manual_cb(lv_event_t *event)
{
    (void)event;
    s_ui.wifi_selected_ssid[0] = '\0';
    s_ui.wifi_stage = WIFI_STAGE_CREDENTIAL_FORM;
    show_view(VIEW_WIFI_CONFIG);
}

static void wifi_access_point_cb(lv_event_t *event)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);

    if(index >= s_ui.model.wifi_scan.count ||
       index >= APP_W800_SCAN_MAX_RESULTS)
        return;
    (void)strncpy(s_ui.wifi_selected_ssid,
                  s_ui.model.wifi_scan.access_points[index].ssid,
                  sizeof(s_ui.wifi_selected_ssid) - 1U);
    s_ui.wifi_selected_ssid[sizeof(s_ui.wifi_selected_ssid) - 1U] = '\0';
    s_ui.wifi_stage = WIFI_STAGE_CREDENTIAL_FORM;
    show_view(VIEW_WIFI_CONFIG);
}

static void wifi_form_cancel_cb(lv_event_t *event)
{
    (void)event;
    if(s_ui.wifi_password != NULL)
        lv_textarea_set_text(s_ui.wifi_password, "");
    s_ui.wifi_stage = WIFI_STAGE_SCAN_LIST;
    show_view(VIEW_WIFI_CONFIG);
}

static void wifi_retry_credentials_cb(lv_event_t *event)
{
    (void)event;
    s_ui.wifi_stage = WIFI_STAGE_CREDENTIAL_FORM;
    show_view(VIEW_WIFI_CONFIG);
}

/** @brief Create one compact status card used on the home page. */
static lv_obj_t *home_card(const char *name, int32_t x, ui_view_t target,
                           lv_obj_t **status)
{
    lv_obj_t *card = action_button(s_ui.content, "", x, 8, 110, 112,
                                   COLOR_PANEL, &lv_font_cjk_18,
                                   view_button_cb, (uintptr_t)target);

    (void)text_label(card, name, 6, 18, 98, &lv_font_cjk_18,
                     COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    *status = text_label(card, "--", 6, 70, 98, &lv_font_cjk_14,
                         COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    return card;
}

static void build_home(void)
{
    lv_obj_t *info;
    lv_obj_t *alert;

    (void)home_card("Wi-Fi", 8, VIEW_NETWORK, &s_ui.dyn.home_wifi);
    (void)home_card("MQTT", 126, VIEW_NETWORK, &s_ui.dyn.home_mqtt);
    (void)home_card("Modbus", 244, VIEW_MODBUS, &s_ui.dyn.home_modbus);
    (void)home_card("设备健康", 362, VIEW_BOARD_TEST, &s_ui.dyn.home_health);

    info = box(s_ui.content, 8, 130, 464, 44, COLOR_PANEL, 10);
    (void)text_label(info, "通信概览", 12, 12, 90, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    s_ui.dyn.home_link = text_label(info, "--", 104, 12, 346,
                                    &lv_font_cjk_14, COLOR_TEXT,
                                    LV_TEXT_ALIGN_RIGHT);

    alert = box(s_ui.content, 8, 182, 464, 42, COLOR_PANEL_ALT, 10);
    s_ui.dyn.home_alert = text_label(alert, "--", 12, 11, 440,
                                     &lv_font_cjk_14, COLOR_GREEN,
                                     LV_TEXT_ALIGN_CENTER);
}

static void build_network(void)
{
    lv_obj_t *panel = box(s_ui.content, 10, 6, 460, 132, COLOR_PANEL, 12);

    (void)status_row(panel, "W800 状态", 0, &s_ui.dyn.net_w800);
    (void)status_row(panel, "BLE 配网", 32, &s_ui.dyn.net_ble);
    (void)status_row(panel, "MQTT", 64, &s_ui.dyn.net_mqtt);
    (void)status_row(panel, "Socket", 96, &s_ui.dyn.net_socket);

    (void)action_button(s_ui.content, "BLE 配网", 10, 148, 146, 48,
                        COLOR_BLUE_DARK, &lv_font_cjk_18,
                        ble_provision_cb, 0U);
    (void)action_button(s_ui.content, "屏幕配置", 167, 148, 146, 48,
                        COLOR_BLUE, &lv_font_cjk_18,
                        view_button_cb, (uintptr_t)VIEW_WIFI_CONFIG);
    (void)action_button(s_ui.content, "重连 MQTT", 324, 148, 146, 48,
                        COLOR_BLUE_DARK, &lv_font_cjk_18,
                        mqtt_reconnect_cb, 0U);
    s_ui.dyn.net_note = text_label(s_ui.content,
                                   "SSID、IP、RSSI 暂无底层查询接口",
                                   12, 207, 456, &lv_font_cjk_12,
                                   COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void build_diagnostics(void)
{
    (void)action_button(s_ui.content, "整板自检\n15 项硬件检查",
                        10, 10, 220, 96, COLOR_PANEL_ALT,
                        &lv_font_cjk_18, view_button_cb,
                        (uintptr_t)VIEW_BOARD_TEST);
    (void)action_button(s_ui.content, "双路 CAN\n收发与错误统计",
                        250, 10, 220, 96, COLOR_PANEL_ALT,
                        &lv_font_cjk_18, view_button_cb,
                        (uintptr_t)VIEW_CAN);
    (void)action_button(s_ui.content, "Modbus / RS485\n主从回环与计数",
                        10, 126, 220, 96, COLOR_PANEL_ALT,
                        &lv_font_cjk_18, view_button_cb,
                        (uintptr_t)VIEW_MODBUS);
    (void)action_button(s_ui.content, "USB 通信\n连接与帧校验",
                        250, 126, 220, 96, COLOR_PANEL_ALT,
                        &lv_font_cjk_18, view_button_cb,
                        (uintptr_t)VIEW_USB);
}

static void build_settings(void)
{
    (void)action_button(s_ui.content, "屏幕亮度                         >",
                        10, 8, 460, 48, COLOR_PANEL, &lv_font_cjk_18,
                        view_button_cb, (uintptr_t)VIEW_BRIGHTNESS);
    (void)action_button(s_ui.content, "时间                             >",
                        10, 64, 460, 48, COLOR_PANEL, &lv_font_cjk_18,
                        view_button_cb, (uintptr_t)VIEW_TIME);
    (void)action_button(s_ui.content, "设备信息                         >",
                        10, 120, 460, 48, COLOR_PANEL, &lv_font_cjk_18,
                        view_button_cb, (uintptr_t)VIEW_DEVICE);
    (void)action_button(s_ui.content, "固件信息                         >",
                        10, 176, 460, 48, COLOR_PANEL, &lv_font_cjk_18,
                        view_button_cb, (uintptr_t)VIEW_FIRMWARE);
}

static void build_wifi_scan_list(void)
{
    const app_w800_scan_snapshot_t *scan = &s_ui.model.wifi_scan;
    char status[48];
    uint8_t visible_count;
    uint8_t index;

    switch(scan->state)
    {
        case APP_W800_SCAN_PENDING:
        case APP_W800_SCAN_RUNNING:
            (void)strncpy(status, "正在扫描附近 Wi-Fi...", sizeof(status) - 1U);
            status[sizeof(status) - 1U] = '\0';
            break;
        case APP_W800_SCAN_READY:
            if(scan->count == 0U)
                (void)strncpy(status, "未发现可用网络", sizeof(status) - 1U);
            else
                (void)snprintf(status, sizeof(status), "发现 %u 个网络",
                               (unsigned)scan->count);
            status[sizeof(status) - 1U] = '\0';
            break;
        case APP_W800_SCAN_FAILED:
            (void)strncpy(status, "扫描失败，请重试", sizeof(status) - 1U);
            status[sizeof(status) - 1U] = '\0';
            break;
        default:
            (void)strncpy(status, "等待扫描", sizeof(status) - 1U);
            status[sizeof(status) - 1U] = '\0';
            break;
    }

    s_ui.wifi_scan_status = text_label(s_ui.content, status, 10, 13, 226,
                                       &lv_font_cjk_14,
                                       scan->state == APP_W800_SCAN_FAILED ?
                                       COLOR_RED : COLOR_MUTED,
                                       LV_TEXT_ALIGN_LEFT);
    (void)action_button(s_ui.content, "手动输入", 246, 4, 106, 38,
                        COLOR_PANEL_ALT, &lv_font_cjk_14,
                        wifi_manual_cb, 0U);
    (void)action_button(s_ui.content, "重新扫描", 362, 4, 108, 38,
                        COLOR_BLUE, &lv_font_cjk_14,
                        wifi_scan_refresh_cb, 0U);

    visible_count = scan->count > 5U ? 5U : scan->count;
    for(index = 0U; index < visible_count; index++)
    {
        const app_w800_access_point_t *access_point =
            &scan->access_points[index];
        lv_obj_t *row;
        char detail[32];

        row = action_button(s_ui.content, "", 10, 50 + (int32_t)index * 44,
                            460, 38, COLOR_PANEL, &lv_font_cjk_14,
                            wifi_access_point_cb, (uintptr_t)index);
        (void)text_label(row, access_point->ssid, 12, 10, 300,
                         &lv_font_montserrat_14, COLOR_TEXT,
                         LV_TEXT_ALIGN_LEFT);
        (void)snprintf(detail, sizeof(detail), "%s  %d dBm",
                       access_point->encryption == 0U ? "开放" : "加密",
                       (int)access_point->rssi_dbm);
        (void)text_label(row, detail, 314, 11, 132,
                         &lv_font_cjk_12, COLOR_MUTED,
                         LV_TEXT_ALIGN_RIGHT);
    }
}

static void build_wifi_credentials(void)
{
    lv_obj_t *label;

    label = text_label(s_ui.content, "SSID", 8, 11, 58,
                       &lv_font_montserrat_14, COLOR_MUTED,
                       LV_TEXT_ALIGN_LEFT);
    (void)label;
    s_ui.wifi_ssid = lv_textarea_create(s_ui.content);
    lv_obj_set_pos(s_ui.wifi_ssid, 64, 4);
    lv_obj_set_size(s_ui.wifi_ssid, 292, 40);
    lv_textarea_set_one_line(s_ui.wifi_ssid, true);
    lv_textarea_set_max_length(s_ui.wifi_ssid, 32U);
    lv_obj_set_style_text_font(s_ui.wifi_ssid, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_ui.wifi_ssid, wifi_field_focused_cb,
                        LV_EVENT_FOCUSED, NULL);
    if(s_ui.wifi_selected_ssid[0] != '\0')
        lv_textarea_set_text(s_ui.wifi_ssid, s_ui.wifi_selected_ssid);

    (void)action_button(s_ui.content, "提交", 366, 4, 104, 40,
                        COLOR_BLUE, &lv_font_cjk_18,
                        wifi_submit_cb, 0U);

    (void)text_label(s_ui.content, "密码", 8, 57, 58,
                     &lv_font_cjk_14, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    s_ui.wifi_password = lv_textarea_create(s_ui.content);
    lv_obj_set_pos(s_ui.wifi_password, 64, 50);
    lv_obj_set_size(s_ui.wifi_password, 292, 40);
    lv_textarea_set_one_line(s_ui.wifi_password, true);
    lv_textarea_set_password_mode(s_ui.wifi_password, true);
    lv_textarea_set_max_length(s_ui.wifi_password, 63U);
    lv_obj_set_style_text_font(s_ui.wifi_password, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_ui.wifi_password, wifi_field_focused_cb,
                        LV_EVENT_FOCUSED, NULL);

    (void)action_button(s_ui.content, "取消", 366, 50, 104, 40,
                        COLOR_PANEL_ALT, &lv_font_cjk_18,
                        wifi_form_cancel_cb, 0U);

    s_ui.wifi_result = text_label(s_ui.content,
                                  "密码仅用于本次提交，不写入日志",
                                  8, 95, 462, &lv_font_cjk_14,
                                  COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_height(s_ui.wifi_result, 20);
    s_ui.wifi_keyboard = lv_keyboard_create(s_ui.content);
    /* lv_keyboard defaults to BOTTOM_MID alignment. Use page coordinates. */
    lv_obj_set_align(s_ui.wifi_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_ui.wifi_keyboard, 4, 116);
    lv_obj_set_size(s_ui.wifi_keyboard, 472, 164);
    lv_obj_set_style_text_font(s_ui.wifi_keyboard, &lv_font_montserrat_14, 0);
    lv_keyboard_set_textarea(s_ui.wifi_keyboard,
                             s_ui.wifi_selected_ssid[0] != '\0' ?
                             s_ui.wifi_password : s_ui.wifi_ssid);
}

static void build_wifi_connecting(void)
{
    lv_obj_t *panel = box(s_ui.content, 20, 18, 440, 170,
                          COLOR_PANEL, 14);

    (void)text_label(panel, "Wi-Fi 配置已提交", 12, 18, 416,
                     &lv_font_cjk_20, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    (void)text_label(panel, s_ui.wifi_selected_ssid, 20, 58, 400,
                     &lv_font_montserrat_14, COLOR_BLUE,
                     LV_TEXT_ALIGN_CENTER);
    s_ui.wifi_connection_status = text_label(
        panel, "请求已接受，等待 W800 处理", 12, 96, 416,
        &lv_font_cjk_14, COLOR_AMBER, LV_TEXT_ALIGN_CENTER);
    (void)text_label(panel, "保存和重新连接通常需要 10 到 20 秒",
                     12, 132, 416, &lv_font_cjk_12,
                     COLOR_MUTED, LV_TEXT_ALIGN_CENTER);

    (void)action_button(s_ui.content, "返回网络", 72, 210, 156, 46,
                        COLOR_PANEL_ALT, &lv_font_cjk_18,
                        view_button_cb, (uintptr_t)VIEW_NETWORK);
    (void)action_button(s_ui.content, "重新输入", 252, 210, 156, 46,
                        COLOR_BLUE_DARK, &lv_font_cjk_18,
                        wifi_retry_credentials_cb, 0U);
}

static void build_wifi_config(void)
{
    if(s_ui.wifi_stage == WIFI_STAGE_CONNECTING)
        build_wifi_connecting();
    else if(s_ui.wifi_stage == WIFI_STAGE_CREDENTIAL_FORM)
        build_wifi_credentials();
    else
        build_wifi_scan_list();
}

static void build_modbus(void)
{
    lv_obj_t *panel = box(s_ui.content, 10, 8, 460, 184, COLOR_PANEL, 12);

    (void)status_row(panel, "本机站号", 0, &s_ui.dyn.modbus_unit);
    (void)status_row(panel, "接收 / 发送", 32, &s_ui.dyn.modbus_frames);
    (void)status_row(panel, "CRC / 异常 / 传输", 64, &s_ui.dyn.modbus_errors);
    (void)status_row(panel, "主机通过 / 失败", 96, &s_ui.dyn.modbus_loopback);
    (void)status_row(panel, "最后寄存器", 128, &s_ui.dyn.modbus_registers);
    (void)text_label(s_ui.content,
                     "当前为只读运行诊断；参数修改由受控配置命令完成",
                     12, 204, 456, &lv_font_cjk_12,
                     COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void build_can(void)
{
    lv_obj_t *panel = box(s_ui.content, 10, 8, 460, 184, COLOR_PANEL, 12);

    (void)status_row(panel, "运行状态", 0, &s_ui.dyn.can_state);
    (void)status_row(panel, "CAN1 / CAN2 帧", 32, &s_ui.dyn.can_counts);
    (void)status_row(panel, "通过 / 失败周期", 64, &s_ui.dyn.can_cycles);
    (void)status_row(panel, "延迟 / 最大延迟", 96, &s_ui.dyn.can_latency);
    (void)status_row(panel, "错误 / Bus-Off", 128, &s_ui.dyn.can_errors);
    (void)text_label(s_ui.content,
                     "双路物理交叉连接自检持续运行",
                     12, 204, 456, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void build_board_test(void)
{
    uint32_t index;

    s_ui.dyn.board_summary = text_label(s_ui.content, "--", 10, 5, 460,
                                        &lv_font_cjk_14, COLOR_TEXT,
                                        LV_TEXT_ALIGN_CENTER);
    for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; index++)
    {
        int32_t column = index < 8U ? 0 : 1;
        int32_t row = index < 8U ? (int32_t)index : (int32_t)(index - 8U);
        s_ui.dyn.board_items[index] = text_label(
            s_ui.content, "--", 10 + column * 232, 32 + row * 22, 220,
            &lv_font_cjk_12, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    }
    (void)action_button(s_ui.content, "开始自检", 150, 205, 180, 38,
                        COLOR_BLUE, &lv_font_cjk_18,
                        run_self_test_cb, 0U);
}

static void build_usb(void)
{
    lv_obj_t *panel = box(s_ui.content, 10, 8, 460, 152, COLOR_PANEL, 12);

    (void)status_row(panel, "USB 状态", 0, &s_ui.dyn.usb_state);
    (void)status_row(panel, "业务帧", 32, &s_ui.dyn.usb_frames);
    (void)status_row(panel, "LDC 字节 / 包", 64, &s_ui.dyn.usb_transport);
    (void)status_row(panel, "CRC / 长度 / 丢弃", 96, &s_ui.dyn.usb_errors);
    (void)text_label(s_ui.content,
                     "USB 串口保留为 Wi-Fi 救援和诊断通道",
                     12, 184, 456, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void build_brightness(void)
{
    lv_obj_t *slider;
    lv_obj_t *panel = box(s_ui.content, 20, 22, 440, 176, COLOR_PANEL, 16);

    (void)text_label(panel, "当前亮度", 20, 24, 130,
                     &lv_font_cjk_18, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    s_ui.dyn.brightness_value = text_label(panel, "--", 278, 20, 140,
                                           &lv_font_montserrat_24,
                                           COLOR_BLUE, LV_TEXT_ALIGN_RIGHT);
    slider = lv_slider_create(panel);
    lv_obj_set_pos(slider, 30, 82);
    lv_obj_set_size(slider, 380, 20);
    lv_slider_set_range(slider, 100, 1000);
    lv_slider_set_value(slider,
                        s_ui.model.brightness_permille >= 100U ?
                        s_ui.model.brightness_permille : 1000U,
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, COLOR_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    (void)text_label(panel, "低", 26, 124, 60, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    (void)text_label(panel, "高", 350, 124, 60, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_RIGHT);
}

static void build_time(void)
{
    lv_obj_t *panel = box(s_ui.content, 30, 28, 420, 150, COLOR_PANEL, 16);

    (void)text_label(panel, "RTC 时间", 20, 18, 380,
                     &lv_font_cjk_18, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    s_ui.dyn.time_value = text_label(panel, "--", 20, 58, 380,
                                     &lv_font_montserrat_24, COLOR_TEXT,
                                     LV_TEXT_ALIGN_CENTER);
    (void)text_label(s_ui.content,
                     "校时请使用 USB 串口受控命令",
                     12, 195, 456, &lv_font_cjk_14,
                     COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void build_device(void)
{
    lv_obj_t *panel = box(s_ui.content, 10, 8, 460, 184, COLOR_PANEL, 12);
    lv_obj_t *value;

    (void)status_row(panel, "目标板", 0, &value);
    set_colored_text(value, "STM32H563RIV6", COLOR_TEXT);
    (void)status_row(panel, "显示屏", 32, &value);
    set_colored_text(value, "ST7796  480x320", COLOR_TEXT);
    (void)status_row(panel, "触摸", 64, &value);
    set_colored_text(value, "FT6336", COLOR_TEXT);
    (void)status_row(panel, "无线模块", 96, &value);
    set_colored_text(value, "W800", COLOR_TEXT);
    (void)status_row(panel, "BSP 版本", 128, &value);
    set_colored_text(value, BSP_VERSION, COLOR_BLUE);
}

static void build_firmware(void)
{
    lv_obj_t *panel = box(s_ui.content, 10, 8, 460, 184, COLOR_PANEL, 12);
    lv_obj_t *value;

    (void)status_row(panel, "应用版本", 0, &value);
    set_colored_text(value, APP_FIRMWARE_VERSION, COLOR_BLUE);
    (void)status_row(panel, "编译日期", 32, &value);
    set_colored_text(value, __DATE__, COLOR_TEXT);
    (void)status_row(panel, "编译时间", 64, &value);
    set_colored_text(value, __TIME__, COLOR_TEXT);
    (void)status_row(panel, "OTA 状态", 96, &s_ui.dyn.firmware_ota);
    (void)status_row(panel, "升级策略", 128, &value);
    set_colored_text(value, "签名包 A/B 回滚", COLOR_GREEN);
    (void)text_label(s_ui.content,
                     "固件升级仅由已签名的软件包触发",
                     12, 204, 456, &lv_font_cjk_12,
                     COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

/** @brief Refresh the header status pill from real health signals. */
static void update_header(void)
{
    uint32_t faults = s_ui.model.board_self_test.failed_count;

    if(faults > 0U)
    {
        lv_obj_set_style_bg_color(s_ui.status_pill, lv_color_hex(0x5B2532), 0);
        set_colored_text(s_ui.status_text, "故障", COLOR_RED);
    }
    else if(s_ui.model.wifi_ready == 0U || s_ui.model.mqtt_online == 0U)
    {
        lv_obj_set_style_bg_color(s_ui.status_pill, lv_color_hex(0x5A4823), 0);
        set_colored_text(s_ui.status_text, "注意", COLOR_AMBER);
    }
    else
    {
        lv_obj_set_style_bg_color(s_ui.status_pill, lv_color_hex(0x194C38), 0);
        set_colored_text(s_ui.status_text, "正常运行", COLOR_GREEN);
    }
}

static const char *self_test_state_text(app_self_test_state_t state)
{
    switch(state)
    {
        case APP_SELF_TEST_STATE_WAITING:   return "等待";
        case APP_SELF_TEST_STATE_RUNNING:   return "运行中";
        case APP_SELF_TEST_STATE_COMPLETED: return "已完成";
        default:                            return "未运行";
    }
}

static const char *self_test_item_cn(app_self_test_item_id_t id)
{
    static const char *const names[APP_SELF_TEST_ITEM_COUNT] =
    {
        "状态灯", "显示屏", "背光", "触摸", "SPI Flash", "W800",
        "W800串口", "调试串口", "RS485-1", "RS485-2", "CAN1", "CAN2",
        "RTC", "USB", "黑匣子"
    };

    return id < APP_SELF_TEST_ITEM_COUNT ? names[id] : "未知";
}

static const char *self_test_result_cn(app_self_test_status_t status)
{
    switch(status)
    {
        case APP_SELF_TEST_STATUS_TESTING:       return "测试中";
        case APP_SELF_TEST_STATUS_NOT_INSTALLED: return "未安装";
        case APP_SELF_TEST_STATUS_NOT_CONNECTED: return "未连接";
        case APP_SELF_TEST_STATUS_FAILED:        return "失败";
        case APP_SELF_TEST_STATUS_PASSED:        return "通过";
        default:                                 return "未测试";
    }
}

static lv_color_t self_test_result_color(app_self_test_status_t status)
{
    switch(status)
    {
        case APP_SELF_TEST_STATUS_PASSED:        return COLOR_GREEN;
        case APP_SELF_TEST_STATUS_FAILED:        return COLOR_RED;
        case APP_SELF_TEST_STATUS_TESTING:       return COLOR_BLUE;
        case APP_SELF_TEST_STATUS_NOT_CONNECTED: return COLOR_AMBER;
        default:                                 return COLOR_MUTED;
    }
}

/** @brief Refresh only the currently instantiated content objects. */
static void update_current_view(void)
{
    char buffer[96];
    const app_can_self_test_snapshot_t *can = &s_ui.model.can_self_test;
    const app_self_test_snapshot_t *test = &s_ui.model.board_self_test;
    uint32_t fault_count;
    uint32_t index;

    switch(s_ui.view)
    {
        case VIEW_HOME:
            set_colored_text(s_ui.dyn.home_wifi,
                             yes_no(s_ui.model.wifi_ready, "已连接", "未连接"),
                             state_color(s_ui.model.wifi_ready));
            set_colored_text(s_ui.dyn.home_mqtt,
                             yes_no(s_ui.model.mqtt_online, "在线", "离线"),
                             state_color(s_ui.model.mqtt_online));
            set_colored_text(s_ui.dyn.home_modbus,
                             s_ui.model.rs485_transport_errors == 0U ? "正常" : "异常",
                             s_ui.model.rs485_transport_errors == 0U ? COLOR_GREEN : COLOR_RED);
            set_colored_text(s_ui.dyn.home_health,
                             test->failed_count == 0U ? "良好" : "待检查",
                             test->failed_count == 0U ? COLOR_GREEN : COLOR_RED);
            (void)snprintf(buffer, sizeof(buffer),
                           "W800 %lu B   RS485 %lu 帧   USB %lu 帧",
                           (unsigned long)s_ui.model.w800_rx_bytes,
                           (unsigned long)s_ui.model.rs485_rx_frames,
                           (unsigned long)s_ui.model.usb_frames);
            set_colored_text(s_ui.dyn.home_link, buffer, COLOR_TEXT);
            fault_count = test->failed_count;
            if(s_ui.model.wifi_ready == 0U) fault_count++;
            if(s_ui.model.mqtt_online == 0U) fault_count++;
            if(s_ui.model.rs485_transport_errors != 0U) fault_count++;
            if(fault_count == 0U)
            {
                set_colored_text(s_ui.dyn.home_alert, "无活动告警", COLOR_GREEN);
            }
            else
            {
                (void)snprintf(buffer, sizeof(buffer), "发现 %lu 项待处理状态",
                               (unsigned long)fault_count);
                set_colored_text(s_ui.dyn.home_alert, buffer, COLOR_AMBER);
            }
            break;

        case VIEW_NETWORK:
            (void)snprintf(buffer, sizeof(buffer), "%s  状态 %u",
                           s_ui.model.wifi_ready != 0U ? "已就绪" : "初始化中",
                           (unsigned)s_ui.model.w800_state);
            set_colored_text(s_ui.dyn.net_w800, buffer,
                             state_color(s_ui.model.wifi_ready));
            (void)snprintf(buffer, sizeof(buffer), "%s  尝试 %lu  超时 %lu",
                           s_ui.model.wifi_provisioning_active != 0U ?
                           "进行中" : "空闲",
                           (unsigned long)s_ui.model.wifi_provision_attempts,
                           (unsigned long)s_ui.model.wifi_provision_timeouts);
            set_colored_text(s_ui.dyn.net_ble, buffer,
                             s_ui.model.wifi_provisioning_active != 0U ?
                             COLOR_BLUE : COLOR_MUTED);
            set_colored_text(s_ui.dyn.net_mqtt,
                             yes_no(s_ui.model.mqtt_online, "在线", "离线"),
                             state_color(s_ui.model.mqtt_online));
            (void)snprintf(buffer, sizeof(buffer), "ID %d  RX %lu  失败 %lu",
                           s_ui.model.w800_socket_id,
                           (unsigned long)s_ui.model.w800_socket_rx_data,
                           (unsigned long)s_ui.model.w800_socket_recv_fail_count);
            set_colored_text(s_ui.dyn.net_socket, buffer,
                             s_ui.model.w800_socket_id >= 0 ? COLOR_GREEN : COLOR_MUTED);
            break;

        case VIEW_WIFI_CONFIG:
            if(s_ui.wifi_stage == WIFI_STAGE_SCAN_LIST &&
               s_ui.model.wifi_scan.generation !=
               s_ui.wifi_scan_generation_seen)
            {
                s_ui.wifi_scan_generation_seen =
                    s_ui.model.wifi_scan.generation;
                show_view(VIEW_WIFI_CONFIG);
                return;
            }
            if(s_ui.wifi_stage == WIFI_STAGE_CONNECTING)
            {
                if(s_ui.model.wifi_ready != 0U ||
                   s_ui.model.wifi_usb_rescue_state ==
                   APP_W800_USB_RESCUE_CONNECTED)
                {
                    set_colored_text(s_ui.wifi_connection_status,
                                     "Wi-Fi 已连接", COLOR_GREEN);
                }
                else
                {
                    switch((app_w800_usb_rescue_state_t)
                           s_ui.model.wifi_usb_rescue_state)
                    {
                        case APP_W800_USB_RESCUE_PENDING:
                            set_colored_text(s_ui.wifi_connection_status,
                                             "凭据已排队，等待 W800", COLOR_AMBER);
                            break;
                        case APP_W800_USB_RESCUE_APPLYING:
                            set_colored_text(s_ui.wifi_connection_status,
                                             "正在保存凭据并重启 W800", COLOR_BLUE);
                            break;
                        case APP_W800_USB_RESCUE_SAVED:
                            set_colored_text(s_ui.wifi_connection_status,
                                             "凭据已保存，正在连接", COLOR_BLUE);
                            break;
                        case APP_W800_USB_RESCUE_FAILED:
                            set_colored_text(s_ui.wifi_connection_status,
                                             "W800 保存失败，请重新输入", COLOR_RED);
                            break;
                        default:
                            set_colored_text(s_ui.wifi_connection_status,
                                             "请求已接受，等待 W800 处理", COLOR_AMBER);
                            break;
                    }
                }
            }
            break;

        case VIEW_MODBUS:
            (void)snprintf(buffer, sizeof(buffer), "%u",
                           (unsigned)s_ui.model.modbus_unit_id);
            set_colored_text(s_ui.dyn.modbus_unit, buffer, COLOR_BLUE);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu",
                           (unsigned long)s_ui.model.rs485_rx_frames,
                           (unsigned long)s_ui.model.rs485_tx_frames);
            set_colored_text(s_ui.dyn.modbus_frames, buffer, COLOR_TEXT);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu / %lu",
                           (unsigned long)s_ui.model.rs485_crc_errors,
                           (unsigned long)s_ui.model.rs485_exceptions,
                           (unsigned long)s_ui.model.rs485_transport_errors);
            set_colored_text(s_ui.dyn.modbus_errors, buffer,
                             (s_ui.model.rs485_crc_errors +
                              s_ui.model.rs485_transport_errors) == 0U ?
                             COLOR_GREEN : COLOR_AMBER);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu",
                           (unsigned long)s_ui.model.modbus_master_passes,
                           (unsigned long)s_ui.model.modbus_master_failures);
            set_colored_text(s_ui.dyn.modbus_loopback, buffer,
                             s_ui.model.modbus_master_failures == 0U ?
                             COLOR_GREEN : COLOR_RED);
            (void)snprintf(buffer, sizeof(buffer), "%u / %u",
                           (unsigned)s_ui.model.modbus_last_register_0,
                           (unsigned)s_ui.model.modbus_last_register_1);
            set_colored_text(s_ui.dyn.modbus_registers, buffer, COLOR_TEXT);
            break;

        case VIEW_CAN:
            set_colored_text(s_ui.dyn.can_state,
                             app_can_self_test_state_name(can->state),
                             can->state == APP_CAN_SELF_TEST_STATE_FAULT ?
                             COLOR_RED : COLOR_GREEN);
            (void)snprintf(buffer, sizeof(buffer), "%lu/%lu  %lu/%lu",
                           (unsigned long)can->can1_tx_frames,
                           (unsigned long)can->can1_rx_frames,
                           (unsigned long)can->can2_tx_frames,
                           (unsigned long)can->can2_rx_frames);
            set_colored_text(s_ui.dyn.can_counts, buffer, COLOR_TEXT);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu",
                           (unsigned long)can->passed_cycles,
                           (unsigned long)can->failed_cycles);
            set_colored_text(s_ui.dyn.can_cycles, buffer,
                             can->failed_cycles == 0U ? COLOR_GREEN : COLOR_AMBER);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu us",
                           (unsigned long)can->last_latency_us,
                           (unsigned long)can->maximum_latency_us);
            set_colored_text(s_ui.dyn.can_latency, buffer, COLOR_BLUE);
            (void)snprintf(buffer, sizeof(buffer), "%lu/%lu  %lu/%lu",
                           (unsigned long)can->can1_error_events,
                           (unsigned long)can->can2_error_events,
                           (unsigned long)can->can1_bus_off_events,
                           (unsigned long)can->can2_bus_off_events);
            set_colored_text(s_ui.dyn.can_errors, buffer,
                             (can->can1_error_events + can->can2_error_events) == 0U ?
                             COLOR_GREEN : COLOR_AMBER);
            break;

        case VIEW_BOARD_TEST:
            (void)snprintf(buffer, sizeof(buffer),
                           "%s  通过 %u  失败 %u  离线 %u  未安装 %u",
                           self_test_state_text(test->state),
                           (unsigned)test->passed_count,
                           (unsigned)test->failed_count,
                           (unsigned)test->not_connected_count,
                           (unsigned)test->not_installed_count);
            set_colored_text(s_ui.dyn.board_summary, buffer,
                             test->failed_count == 0U ? COLOR_GREEN : COLOR_RED);
            for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; index++)
            {
                (void)snprintf(buffer, sizeof(buffer), "%s  %s",
                               self_test_item_cn(test->items[index].id),
                               self_test_result_cn(test->items[index].status));
                set_colored_text(s_ui.dyn.board_items[index], buffer,
                                 self_test_result_color(test->items[index].status));
            }
            break;

        case VIEW_USB:
            set_colored_text(s_ui.dyn.usb_state,
                             yes_no(s_ui.model.usb_connected, "已连接", "未连接"),
                             state_color(s_ui.model.usb_connected));
            (void)snprintf(buffer, sizeof(buffer), "%lu",
                           (unsigned long)s_ui.model.usb_frames);
            set_colored_text(s_ui.dyn.usb_frames, buffer, COLOR_TEXT);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu",
                           (unsigned long)s_ui.model.usb_rx_bytes,
                           (unsigned long)s_ui.model.usb_packets);
            set_colored_text(s_ui.dyn.usb_transport, buffer, COLOR_TEXT);
            (void)snprintf(buffer, sizeof(buffer), "%lu / %lu / %lu",
                           (unsigned long)s_ui.model.usb_crc_errors,
                           (unsigned long)s_ui.model.usb_length_errors,
                           (unsigned long)s_ui.model.usb_discarded_bytes);
            set_colored_text(s_ui.dyn.usb_errors, buffer,
                             (s_ui.model.usb_crc_errors +
                              s_ui.model.usb_length_errors) == 0U ?
                             COLOR_GREEN : COLOR_AMBER);
            break;

        case VIEW_BRIGHTNESS:
            (void)snprintf(buffer, sizeof(buffer), "%u%%",
                           (unsigned)(s_ui.model.brightness_permille / 10U));
            set_colored_text(s_ui.dyn.brightness_value, buffer, COLOR_BLUE);
            break;

        case VIEW_TIME:
            if(s_ui.model.rtc_valid != 0U)
            {
                (void)snprintf(buffer, sizeof(buffer),
                               "%04u-%02u-%02u  %02u:%02u:%02u",
                               (unsigned)s_ui.model.rtc_year,
                               (unsigned)s_ui.model.rtc_month,
                               (unsigned)s_ui.model.rtc_day,
                               (unsigned)s_ui.model.rtc_hour,
                               (unsigned)s_ui.model.rtc_minute,
                               (unsigned)s_ui.model.rtc_second);
                set_colored_text(s_ui.dyn.time_value, buffer, COLOR_TEXT);
            }
            else
            {
                set_colored_text(s_ui.dyn.time_value, "RTC 尚未设置", COLOR_AMBER);
            }
            break;

        case VIEW_FIRMWARE:
            if(s_ui.model.ota_active != 0U)
            {
                (void)snprintf(buffer, sizeof(buffer), "升级中  %lu / %lu",
                               (unsigned long)s_ui.model.ota_received,
                               (unsigned long)s_ui.model.ota_expected);
                set_colored_text(s_ui.dyn.firmware_ota, buffer, COLOR_BLUE);
            }
            else
            {
                set_colored_text(s_ui.dyn.firmware_ota, "空闲", COLOR_GREEN);
            }
            break;

        default:
            break;
    }
}

/** @brief Update persistent navigation colors and back-button visibility. */
static void update_shell_for_view(void)
{
    uint32_t index;
    uint8_t secondary = (s_ui.view != VIEW_HOME &&
                         s_ui.view != VIEW_NETWORK &&
                         s_ui.view != VIEW_DIAGNOSTICS &&
                         s_ui.view != VIEW_SETTINGS) ? 1U : 0U;

    lv_label_set_text(s_ui.title, title_for_view(s_ui.view));
    if(secondary != 0U)
    {
        lv_obj_remove_flag(s_ui.back_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_ui.title, 54);
        lv_obj_set_width(s_ui.title, 260);
    }
    else
    {
        lv_obj_add_flag(s_ui.back_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_ui.title, 14);
        lv_obj_set_width(s_ui.title, 300);
    }

    for(index = 0U; index < 4U; index++)
    {
        lv_obj_set_style_bg_color(s_ui.nav_button[index],
                                  index == s_ui.primary_page ?
                                  COLOR_BLUE : COLOR_HEADER, 0);
    }
}

/** @brief Rebuild one content page; no old page objects remain allocated. */
static void show_view(ui_view_t view)
{
    s_ui.view = view;
    s_ui.primary_page = primary_for_view(view);
    s_ui.back_view = s_ui.primary_page == 1U ? VIEW_NETWORK :
                     s_ui.primary_page == 2U ? VIEW_DIAGNOSTICS :
                     s_ui.primary_page == 3U ? VIEW_SETTINGS : VIEW_HOME;

    lv_obj_clean(s_ui.content);
    (void)memset(&s_ui.dyn, 0, sizeof(s_ui.dyn));
    s_ui.wifi_ssid = NULL;
    s_ui.wifi_password = NULL;
    s_ui.wifi_keyboard = NULL;
    s_ui.wifi_result = NULL;
    s_ui.wifi_scan_status = NULL;
    s_ui.wifi_connection_status = NULL;

    if(view == VIEW_WIFI_CONFIG)
    {
        lv_obj_add_flag(s_ui.nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_ui.content, UI_H - UI_HEADER_H);
    }
    else
    {
        lv_obj_remove_flag(s_ui.nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_ui.content, UI_CONTENT_H);
    }

    switch(view)
    {
        case VIEW_HOME:        build_home(); break;
        case VIEW_NETWORK:     build_network(); break;
        case VIEW_DIAGNOSTICS: build_diagnostics(); break;
        case VIEW_SETTINGS:    build_settings(); break;
        case VIEW_WIFI_CONFIG: build_wifi_config(); break;
        case VIEW_MODBUS:      build_modbus(); break;
        case VIEW_CAN:         build_can(); break;
        case VIEW_BOARD_TEST:  build_board_test(); break;
        case VIEW_USB:         build_usb(); break;
        case VIEW_BRIGHTNESS:  build_brightness(); break;
        case VIEW_TIME:        build_time(); break;
        case VIEW_DEVICE:      build_device(); break;
        case VIEW_FIRMWARE:    build_firmware(); break;
        default:               build_home(); break;
    }

    update_shell_for_view();
    update_current_view();
}

/** @brief Construct the persistent product shell. */
static void create_shell(void)
{
    static const char *const nav_names[4] =
    {
        "首页", "网络", "诊断", "设置"
    };
    uint32_t index;

    s_ui.shell = box(s_ui.screen, 0, 0, UI_W, UI_H, COLOR_BG, 0);
    s_ui.header = box(s_ui.shell, 0, 0, UI_W, UI_HEADER_H, COLOR_HEADER, 0);
    s_ui.back_button = action_button(s_ui.header, "<", 4, 3, 42, 32,
                                     COLOR_PANEL_ALT, &lv_font_montserrat_20,
                                     back_button_cb, 0U);
    s_ui.title = text_label(s_ui.header, "系统状态", 14, 8, 300,
                            &lv_font_cjk_20, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    s_ui.status_pill = box(s_ui.header, 362, 5, 108, 28,
                           lv_color_hex(0x194C38), 12);
    s_ui.status_text = text_label(s_ui.status_pill, "正常运行", 4, 5, 100,
                                  &lv_font_cjk_14, COLOR_GREEN,
                                  LV_TEXT_ALIGN_CENTER);

    s_ui.content = box(s_ui.shell, 0, UI_HEADER_H, UI_W, UI_CONTENT_H,
                       COLOR_BG, 0);
    s_ui.nav = box(s_ui.shell, 0, UI_H - UI_NAV_H, UI_W, UI_NAV_H,
                   COLOR_HEADER, 0);
    for(index = 0U; index < 4U; index++)
    {
        s_ui.nav_button[index] = action_button(
            s_ui.nav, nav_names[index], (int32_t)(index * 120U), 0,
            120, UI_NAV_H, index == 0U ? COLOR_BLUE : COLOR_HEADER,
            &lv_font_cjk_18, nav_button_cb, (uintptr_t)index);
        lv_obj_set_style_radius(s_ui.nav_button[index], 0, 0);
    }
}

/** @brief Position black eyelid masks around the visible eye gap. */
static void set_eye_gap(int32_t gap)
{
    int32_t top_height = UI_BOOT_EYE_Y - gap / 2;
    int32_t bottom_y = UI_BOOT_EYE_Y + gap / 2;

    if(top_height < 0) top_height = 0;
    if(bottom_y > UI_H) bottom_y = UI_H;
    lv_obj_set_height(s_ui.boot_top_lid, top_height);
    lv_obj_set_y(s_ui.boot_bottom_lid, bottom_y);
    lv_obj_set_height(s_ui.boot_bottom_lid, UI_H - bottom_y);
}

static void eye_gap_anim_cb(void *object, int32_t value)
{
    (void)object;
    set_eye_gap(value);
}

static void boot_flash_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

/** @brief Reveal the shell and delete all boot-animation objects. */
static void finish_boot_cb(lv_timer_t *timer)
{
    lv_obj_remove_flag(s_ui.shell, LV_OBJ_FLAG_HIDDEN);
    if(s_ui.boot_layer != NULL)
    {
        lv_obj_delete(s_ui.boot_layer);
        s_ui.boot_layer = NULL;
        s_ui.boot_top_lid = NULL;
        s_ui.boot_bottom_lid = NULL;
        s_ui.boot_flash = NULL;
    }
    lv_timer_delete(timer);
}

/** @brief Create the cinematic eye layer and its non-blocking reveal. */
static void create_boot_layer(void)
{
    lv_obj_t *image;
    lv_anim_t animation;

    s_ui.boot_layer = box(s_ui.screen, 0, 0, UI_W, UI_H, COLOR_BLACK, 0);
    image = lv_image_create(s_ui.boot_layer);
    lv_image_set_src(image, &boot_eye_img);
    lv_obj_set_pos(image, 0, 0);

    s_ui.boot_flash = box(s_ui.boot_layer, 0, 0, UI_W, UI_H,
                          lv_color_hex(0xB00038), 0);
    lv_obj_set_style_bg_opa(s_ui.boot_flash, LV_OPA_TRANSP, 0);
    s_ui.boot_top_lid = box(s_ui.boot_layer, 0, 0, UI_W,
                            UI_BOOT_EYE_Y, COLOR_BLACK, 0);
    s_ui.boot_bottom_lid = box(s_ui.boot_layer, 0, UI_BOOT_EYE_Y, UI_W,
                               UI_H - UI_BOOT_EYE_Y, COLOR_BLACK, 0);
    set_eye_gap(0);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, NULL);
    lv_anim_set_values(&animation, 0, 132);
    lv_anim_set_delay(&animation, 180U);
    lv_anim_set_time(&animation, 900U);
    lv_anim_set_exec_cb(&animation, eye_gap_anim_cb);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_ui.boot_flash);
    lv_anim_set_values(&animation, 0, 46);
    lv_anim_set_delay(&animation, 820U);
    lv_anim_set_time(&animation, 100U);
    lv_anim_set_playback_time(&animation, 260U);
    lv_anim_set_exec_cb(&animation, boot_flash_anim_cb);
    lv_anim_start(&animation);

    lv_timer_create(finish_boot_cb, 1350U, NULL);
}

void sim_ui_init(void)
{
    (void)memset(&s_ui, 0, sizeof(s_ui));
    s_ui.screen = lv_screen_active();
    lv_obj_remove_style_all(s_ui.screen);
    lv_obj_set_style_bg_color(s_ui.screen, COLOR_BLACK, 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    app_ui_model_get_snapshot(&s_ui.model);
    create_shell();
    show_view(VIEW_HOME);
    update_header();
    lv_obj_add_flag(s_ui.shell, LV_OBJ_FLAG_HIDDEN);
    create_boot_layer();
}

void sim_ui_update(void)
{
    app_ui_model_get_snapshot(&s_ui.model);
    update_header();
    update_current_view();
}

int sim_ui_set_page(sim_ui_page_t page)
{
    switch(page)
    {
        case SIM_UI_PAGE_DASHBOARD:
            show_view(VIEW_HOME);
            return 0;
        case SIM_UI_PAGE_COMM:
            show_view(VIEW_NETWORK);
            return 0;
        case SIM_UI_PAGE_CAN_SELF_TEST:
            show_view(VIEW_CAN);
            return 0;
        case SIM_UI_PAGE_BOARD_SELF_TEST:
            show_view(VIEW_BOARD_TEST);
            return 0;
        default:
            return -1;
    }
}

sim_ui_page_t sim_ui_get_page(void)
{
    if(s_ui.view == VIEW_CAN)
    {
        return SIM_UI_PAGE_CAN_SELF_TEST;
    }
    if(s_ui.view == VIEW_BOARD_TEST)
    {
        return SIM_UI_PAGE_BOARD_SELF_TEST;
    }
    if(s_ui.primary_page == 1U)
    {
        return SIM_UI_PAGE_COMM;
    }
    return SIM_UI_PAGE_DASHBOARD;
}
