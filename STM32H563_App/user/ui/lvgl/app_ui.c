#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_ui_model.h"
#include "lv_port_disp.h"
#include "lvgl.h"

#define APP_UI_TICK_MS             5UL
#define APP_UI_REFRESH_PERIOD_MS   500UL

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *w800_value;
    lv_obj_t *w800_hint;
    lv_obj_t *nearlink_value;
    lv_obj_t *nearlink_hint;
    lv_obj_t *rs485_value;
    lv_obj_t *rs485_hint;
    lv_obj_t *usb_value;
    lv_obj_t *usb_hint;
    lv_obj_t *footer;
} app_ui_comm_view_t;

static volatile app_ui_page_t s_requested_page = APP_UI_PAGE_DASHBOARD;
static app_ui_page_t s_current_page = APP_UI_PAGE_DASHBOARD;
static app_ui_comm_view_t s_comm_view;

static void app_ui_set_bg(lv_obj_t *screen, lv_color_t top, lv_color_t bottom)
{
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, top, 0);
    lv_obj_set_style_bg_grad_color(screen, bottom, 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
}

static lv_obj_t *app_ui_make_panel(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t w,
                                   int32_t h,
                                   lv_color_t color)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, color, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 10, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 6, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    return panel;
}

static void app_ui_add_status_card(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t w,
                                   int32_t h,
                                   lv_color_t color,
                                   const char *title,
                                   const char *value,
                                   const char *hint)
{
    lv_obj_t *card = app_ui_make_panel(parent, x, y, w, h, lv_color_white());

    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 28, 28);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x66708A), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 40, 0);

    lv_obj_t *main_value = lv_label_create(card);
    lv_label_set_text(main_value, value);
    lv_obj_set_style_text_font(main_value, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(main_value, lv_color_hex(0x25315F), 0);
    lv_obj_align(main_value, LV_ALIGN_TOP_LEFT, 40, 18);

    lv_obj_t *hint_label = lv_label_create(card);
    lv_label_set_text(hint_label, hint);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x8A93AA), 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static lv_obj_t *app_ui_add_comm_card(lv_obj_t *parent,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h,
                                      lv_color_t dot_color,
                                      const char *title,
                                      lv_obj_t **value_label,
                                      lv_obj_t **hint_label)
{
    lv_obj_t *card = app_ui_make_panel(parent, x, y, w, h, lv_color_white());

    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 18, 18);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, dot_color, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 0, 2);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x66708A), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 28, 0);

    *value_label = lv_label_create(card);
    lv_label_set_text(*value_label, "--");
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0x25315F), 0);
    lv_obj_align(*value_label, LV_ALIGN_TOP_LEFT, 0, 30);

    *hint_label = lv_label_create(card);
    lv_label_set_text(*hint_label, "--");
    lv_obj_set_style_text_font(*hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(*hint_label, lv_color_hex(0x8A93AA), 0);
    lv_obj_align(*hint_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return card;
}

static void app_ui_create_dashboard(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    app_ui_set_bg(screen, lv_color_hex(0xDFF6FF), lv_color_hex(0xFFF1D6));

    lv_obj_t *mascot = lv_obj_create(screen);
    lv_obj_remove_style_all(mascot);
    lv_obj_set_size(mascot, 72, 72);
    lv_obj_set_style_radius(mascot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mascot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mascot, lv_color_hex(0x9BF6FF), 0);
    lv_obj_set_style_border_width(mascot, 5, 0);
    lv_obj_set_style_border_color(mascot, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(mascot, 12, 0);
    lv_obj_set_style_shadow_opa(mascot, LV_OPA_20, 0);
    lv_obj_set_pos(mascot, 24, 22);

    lv_obj_t *face = lv_label_create(mascot);
    lv_label_set_text(face, ":)");
    lv_obj_set_style_text_font(face, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(face, lv_color_hex(0x26547C), 0);
    lv_obj_center(face);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "H563 Buddy");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25315F), 0);
    lv_obj_set_pos(title, 116, 26);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Dashboard online  |  shell: ui page comm");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x65708E), 0);
    lv_obj_set_pos(subtitle, 118, 64);

    app_ui_add_status_card(screen, 20, 120, 136, 72, lv_color_hex(0x06D6A0), "W800", "WiFi", "MQTT link");
    app_ui_add_status_card(screen, 172, 120, 136, 72, lv_color_hex(0xFF6B6B), "NearLink", "SLE", "UART3 AT");
    app_ui_add_status_card(screen, 324, 120, 136, 72, lv_color_hex(0x4D96FF), "RS485", "Modbus", "bus ready");

    app_ui_add_status_card(screen, 20, 212, 136, 72, lv_color_hex(0xFFD166), "USB", "Shell", "CDC ACM");
    app_ui_add_status_card(screen, 172, 212, 136, 72, lv_color_hex(0xB8F2E6), "LDC", "Core", "packet IO");
    app_ui_add_status_card(screen, 324, 212, 136, 72, lv_color_hex(0xCDB4DB), "UI", "LVGL", "v9.2.2");

    lv_screen_load(screen);
    s_current_page = APP_UI_PAGE_DASHBOARD;
}

static void app_ui_create_comm_page(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    app_ui_set_bg(screen, lv_color_hex(0xEAF7FF), lv_color_hex(0xF7E8FF));

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Comm Monitor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25315F), 0);
    lv_obj_set_pos(title, 22, 18);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "W800 / NearLink / RS485 / USB");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x65708E), 0);
    lv_obj_set_pos(subtitle, 24, 54);

    lv_obj_t *pill = lv_obj_create(screen);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 150, 34);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0xB8F2E6), 0);
    lv_obj_set_pos(pill, 306, 22);

    lv_obj_t *pill_text = lv_label_create(pill);
    lv_label_set_text(pill_text, "ui page dashboard");
    lv_obj_set_style_text_font(pill_text, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pill_text, lv_color_hex(0x1B6B6F), 0);
    lv_obj_center(pill_text);

    app_ui_add_comm_card(screen, 20, 92, 210, 86, lv_color_hex(0x06D6A0),
                         "W800 WiFi/MQTT", &s_comm_view.w800_value, &s_comm_view.w800_hint);
    app_ui_add_comm_card(screen, 250, 92, 210, 86, lv_color_hex(0xFF6B6B),
                         "NearLink SLE", &s_comm_view.nearlink_value, &s_comm_view.nearlink_hint);
    app_ui_add_comm_card(screen, 20, 196, 210, 86, lv_color_hex(0x4D96FF),
                         "RS485 Modbus", &s_comm_view.rs485_value, &s_comm_view.rs485_hint);
    app_ui_add_comm_card(screen, 250, 196, 210, 86, lv_color_hex(0xFFD166),
                         "USB Shell", &s_comm_view.usb_value, &s_comm_view.usb_hint);

    s_comm_view.footer = lv_label_create(screen);
    lv_label_set_text(s_comm_view.footer, "refresh: 500ms");
    lv_obj_set_style_text_font(s_comm_view.footer, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_comm_view.footer, lv_color_hex(0x65708E), 0);
    lv_obj_set_pos(s_comm_view.footer, 22, 298);

    s_comm_view.root = screen;
    lv_screen_load(screen);
    s_current_page = APP_UI_PAGE_COMM;
}

static void app_ui_comm_update(void)
{
    app_ui_model_snapshot_t snapshot;
    char value[64];
    char hint[96];

    if(s_current_page != APP_UI_PAGE_COMM || s_comm_view.root == NULL)
    {
        return;
    }

    app_ui_model_get_snapshot(&snapshot);

    (void)snprintf(value, sizeof(value), "%s / %s",
                   snapshot.wifi_ready ? "WiFi OK" : "WiFi --",
                   snapshot.mqtt_online ? "MQTT OK" : "MQTT --");
    (void)snprintf(hint, sizeof(hint), "socket=%d rx=%lu pkt=%lu",
                   snapshot.w800_socket_id,
                   (unsigned long)snapshot.w800_rx_bytes,
                   (unsigned long)snapshot.w800_packets);
    lv_label_set_text(s_comm_view.w800_value, value);
    lv_label_set_text(s_comm_view.w800_hint, hint);

    (void)snprintf(value, sizeof(value), "%s %s",
                   snapshot.nearlink_is_server ? "Server" : "Client",
                   snapshot.nearlink_connected ? "Link" :
                   (snapshot.nearlink_pending ? "Pending" :
                    (snapshot.nearlink_active ? "Active" : "--")));
    (void)snprintf(hint, sizeof(hint), "local=%s rx=%lu pkt=%lu",
                   snapshot.nearlink_local_name[0] ? snapshot.nearlink_local_name : "-",
                   (unsigned long)snapshot.nearlink_rx_bytes,
                   (unsigned long)snapshot.nearlink_packets);
    lv_label_set_text(s_comm_view.nearlink_value, value);
    lv_label_set_text(s_comm_view.nearlink_hint, hint);

    (void)snprintf(value, sizeof(value), "RX %lu / TX %lu",
                   (unsigned long)snapshot.rs485_rx_frames,
                   (unsigned long)snapshot.rs485_tx_frames);
    (void)snprintf(hint, sizeof(hint), "crc=%lu ldc=%luB/%lup",
                   (unsigned long)snapshot.rs485_crc_errors,
                   (unsigned long)snapshot.rs485_rx_bytes,
                   (unsigned long)snapshot.rs485_packets);
    lv_label_set_text(s_comm_view.rs485_value, value);
    lv_label_set_text(s_comm_view.rs485_hint, hint);

    (void)snprintf(value, sizeof(value), "%s",
                   snapshot.usb_connected ? "Connected" : "Offline");
    (void)snprintf(hint, sizeof(hint), "frames=%lu crc=%lu ldc=%luB/%lup",
                   (unsigned long)snapshot.usb_frames,
                   (unsigned long)snapshot.usb_crc_errors,
                   (unsigned long)snapshot.usb_rx_bytes,
                   (unsigned long)snapshot.usb_packets);
    lv_label_set_text(s_comm_view.usb_value, value);
    lv_label_set_text(s_comm_view.usb_hint, hint);
}

static void app_ui_apply_requested_page(void)
{
    app_ui_page_t requested = s_requested_page;

    if(requested == s_current_page)
    {
        return;
    }

    if(requested == APP_UI_PAGE_COMM)
    {
        (void)memset(&s_comm_view, 0, sizeof(s_comm_view));
        app_ui_create_comm_page();
        app_ui_comm_update();
    }
    else
    {
        (void)memset(&s_comm_view, 0, sizeof(s_comm_view));
        app_ui_create_dashboard();
    }
}

UINT app_ui_init(void)
{
    lv_init();

    if(lv_port_disp_init() == NULL)
    {
        return TX_PTR_ERROR;
    }

    app_ui_create_dashboard();
    return TX_SUCCESS;
}

int app_ui_request_page(app_ui_page_t page)
{
    if(page != APP_UI_PAGE_DASHBOARD && page != APP_UI_PAGE_COMM)
    {
        return -1;
    }

    s_requested_page = page;
    return 0;
}

app_ui_page_t app_ui_get_page(void)
{
    return s_current_page;
}

void app_ui_task_entry(ULONG thread_input)
{
    ULONG elapsed_ms = 0UL;

    TX_PARAMETER_NOT_USED(thread_input);

    for(;;)
    {
        lv_tick_inc(APP_UI_TICK_MS);
        app_ui_apply_requested_page();

        elapsed_ms += APP_UI_TICK_MS;
        if(elapsed_ms >= APP_UI_REFRESH_PERIOD_MS)
        {
            elapsed_ms = 0UL;
            app_ui_comm_update();
        }

        (void)lv_timer_handler();
        tx_thread_sleep(APP_UI_TICK_MS);
    }
}
