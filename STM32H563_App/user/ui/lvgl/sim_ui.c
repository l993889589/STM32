/*
 * Reference-image based LVGL pages.
 *
 * This UI keeps the boot eye animation, then shows the five reference pictures
 * from assets/ui/source as full-screen LVGL image pages. The pictures remain
 * the base artwork; only numeric slots are covered with small dark value fields
 * and refreshed from sim_data_t.
 *
 * Page order:
 *   0: brand
 *   1: CPU/GPU monitor
 *   2: sensors and disk status
 *   3: event list
 *   4: communication
 *
 * Usage:
 *   sim_ui_init() creates the boot layer, page image, touch zones, and dynamic
 *   value labels.
 *   sim_ui_update() refreshes the numeric overlays. Call it periodically after
 *   sim_data_tick().
 *
 * Page switching:
 *   Tap/click the left half for previous page and the right half for next page.
 *   A timer also advances to the next page every 20 seconds.
 */
#include "sim_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sim_data.h"
#include "ui_asset_store.h"
#include "misc/cache/lv_image_cache.h"

extern const lv_image_dsc_t boot_eye_img;

#define UI_W                    480
#define UI_H                    320
#define EYE_CENTER_Y            160
#define OVERLAY_CAP             48U
#define AUTO_PAGE_PERIOD_MS     20000U

#define COL_BLACK               lv_color_hex(0x000000)
#define COL_FLASH               lv_color_hex(0xA642FF)
#define COL_TEXT                lv_color_hex(0xEBD6FF)
#define COL_VALUE               lv_color_hex(0x2BA8FF)
#define COL_VALUE_HOT           lv_color_hex(0x50F2FF)
#define COL_PANEL               lv_color_hex(0x010A15)

typedef uint8_t ref_page_t;

typedef struct
{
    lv_obj_t *cpu_usage;
    lv_obj_t *gpu_usage;
    lv_obj_t *cpu_temp;
    lv_obj_t *gpu_temp;
    lv_obj_t *cpu_freq;
    lv_obj_t *board_voltage;
    lv_obj_t *fan_load;
    lv_obj_t *monitor_time;

    lv_obj_t *rear_temp;
    lv_obj_t *front_temp;
    lv_obj_t *humidity;
    lv_obj_t *sensor_time;

    lv_obj_t *w800_rssi;
    lv_obj_t *modbus_stations;
    lv_obj_t *mqtt_qos;
    lv_obj_t *msg_rate;
    lv_obj_t *msg_processed;
    lv_obj_t *msg_dropped;
    lv_obj_t *comm_time;
} dyn_labels_t;

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *boot_layer;
    lv_obj_t *boot_top_lid;
    lv_obj_t *boot_bottom_lid;
    lv_obj_t *boot_flash;
    lv_obj_t *page_layer;
    lv_obj_t *page_img;
    lv_obj_t *fallback_label;
    lv_obj_t *overlays[UI_ASSET_PAGE_COUNT][OVERLAY_CAP];
    uint8_t overlay_count[UI_ASSET_PAGE_COUNT];
    dyn_labels_t dyn;
    ref_page_t page;
    uint32_t asset_generation_seen;
    uint8_t asset_available_seen;
} ui_state_t;

static ui_state_t s_ui;

static void set_page(ref_page_t page);

static void overlay_register(ref_page_t page, lv_obj_t *obj)
{
    uint8_t idx;

    if(page >= UI_ASSET_PAGE_COUNT)
        return;

    idx = s_ui.overlay_count[page];
    if(idx >= OVERLAY_CAP)
        return;

    s_ui.overlays[page][idx] = obj;
    s_ui.overlay_count[page] = (uint8_t)(idx + 1U);
}

static void overlay_show(ref_page_t page)
{
    for(uint32_t p = 0U; p < UI_ASSET_PAGE_COUNT; p++)
    {
        for(uint32_t i = 0U; i < s_ui.overlay_count[p]; i++)
        {
            if(p == page)
                lv_obj_remove_flag(s_ui.overlays[p][i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(s_ui.overlays[p][i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *rect(lv_obj_t *parent, int32_t x, int32_t y,
                      int32_t w, int32_t h, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int32_t x, int32_t y,
                       const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, color, 0);
    return o;
}

static lv_obj_t *value_label(ref_page_t page, int32_t x, int32_t y, int32_t w,
                             const lv_font_t *font, lv_color_t color,
                             lv_color_t background)
{
    lv_obj_t *o = label(s_ui.page_layer, "--", x, y, font, color);
    lv_obj_set_width(o, w);
    if(font != NULL)
        lv_obj_set_height(o, (int32_t)font->line_height + 4);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    /* External-Flash images are decoded in strips. Give each changing field an
       opaque background so a text refresh cannot retain pixels from the old
       value when LVGL redraws only the label's invalidated rectangle. */
    lv_obj_set_style_bg_color(o, background, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    overlay_register(page, o);
    return o;
}

static void set_label(lv_obj_t *obj, const char *text)
{
    if(obj != NULL)
        lv_label_set_text(obj, text);
}

static void drop_asset_page_cache(void)
{
    for(uint32_t i = 0U; i < UI_ASSET_PAGE_COUNT; i++)
    {
        const lv_image_dsc_t *src = ui_asset_store_page_src(i);
        if(src != NULL)
            lv_image_cache_drop(src);
    }
}

static void refresh_asset_binding_if_needed(void)
{
    uint32_t generation = ui_asset_store_generation();
    uint8_t available = ui_asset_store_available() ? 1U : 0U;

    if(generation == s_ui.asset_generation_seen &&
       available == s_ui.asset_available_seen)
    {
        return;
    }

    s_ui.asset_generation_seen = generation;
    s_ui.asset_available_seen = available;
    drop_asset_page_cache();

    /* The LVGL source descriptors have stable addresses, so a committed
       external-Flash slot must explicitly rebind and invalidate the image. */
    set_page(s_ui.page);
    if(s_ui.page_layer != NULL)
        lv_obj_invalidate(s_ui.page_layer);
}

static void set_eye_gap(int32_t gap)
{
    int32_t top_h = EYE_CENTER_Y - (gap / 2);
    int32_t bottom_y = EYE_CENTER_Y + (gap / 2);

    if(top_h < 0)
        top_h = 0;
    if(bottom_y > UI_H)
        bottom_y = UI_H;

    lv_obj_set_height(s_ui.boot_top_lid, top_h);
    lv_obj_set_y(s_ui.boot_bottom_lid, bottom_y);
    lv_obj_set_height(s_ui.boot_bottom_lid, UI_H - bottom_y);
}

static void eye_gap_cb(void *var, int32_t v)
{
    (void)var;
    set_eye_gap(v);
}

static void flash_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void start_boot_animation(void)
{
    lv_anim_t a;
    set_eye_gap(2);

    lv_anim_init(&a);
    lv_anim_set_var(&a, NULL);
    lv_anim_set_values(&a, 2, 120);
    lv_anim_set_delay(&a, 220);
    lv_anim_set_time(&a, 850);
    lv_anim_set_exec_cb(&a, eye_gap_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ui.boot_flash);
    lv_anim_set_values(&a, 0, 90);
    lv_anim_set_delay(&a, 760);
    lv_anim_set_time(&a, 120);
    lv_anim_set_playback_time(&a, 360);
    lv_anim_set_exec_cb(&a, flash_cb);
    lv_anim_start(&a);
}

static void create_boot_layer(void)
{
    lv_obj_t *img;
    lv_obj_t *title;

    s_ui.boot_layer = rect(s_ui.screen, 0, 0, UI_W, UI_H, COL_BLACK, LV_OPA_COVER);
    img = lv_image_create(s_ui.boot_layer);
    lv_image_set_src(img, &boot_eye_img);
    lv_obj_set_pos(img, 0, 0);

    s_ui.boot_flash = rect(s_ui.boot_layer, 0, 0, UI_W, UI_H, COL_FLASH, LV_OPA_TRANSP);
    s_ui.boot_top_lid = rect(s_ui.boot_layer, 0, 0, UI_W, EYE_CENTER_Y, COL_BLACK, LV_OPA_COVER);
    s_ui.boot_bottom_lid = rect(s_ui.boot_layer, 0, EYE_CENTER_Y, UI_W, UI_H - EYE_CENTER_Y,
                                COL_BLACK, LV_OPA_COVER);

    title = label(s_ui.boot_layer, "CORE AWAKENING", 0, 286, &lv_font_montserrat_18, COL_TEXT);
    lv_obj_set_width(title, UI_W);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(title, LV_OPA_60, 0);
}

static void set_page(ref_page_t page)
{
    if(page >= UI_ASSET_PAGE_COUNT)
        page = UI_ASSET_PAGE_BRAND;

    s_ui.page = page;
    const lv_image_dsc_t *src = ui_asset_store_page_src(page);
    if(src != NULL)
    {
        lv_image_set_src(s_ui.page_img, src);
        lv_obj_remove_flag(s_ui.page_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.fallback_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_ui.page_img, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_ui.fallback_label, "UI ASSET %s\nv%lu",
                              ui_asset_store_status(),
                              (unsigned long)ui_asset_store_active_version());
        lv_obj_remove_flag(s_ui.fallback_label, LV_OBJ_FLAG_HIDDEN);
    }
    overlay_show(page);
}

static void switch_zone_cb(lv_event_t *e)
{
    uintptr_t dir = (uintptr_t)lv_event_get_user_data(e);
    uint32_t page = (uint32_t)s_ui.page;

    if(dir == 0U)
        page = (page + UI_ASSET_PAGE_COUNT - 1U) % UI_ASSET_PAGE_COUNT;
    else
        page = (page + 1U) % UI_ASSET_PAGE_COUNT;

    set_page((ref_page_t)page);
}

static void auto_page_cb(lv_timer_t *timer)
{
    (void)timer;
    set_page((ref_page_t)(((uint32_t)s_ui.page + 1U) % UI_ASSET_PAGE_COUNT));
}

static void create_monitor_overlays(void)
{
    const ref_page_t page = UI_ASSET_PAGE_MONITOR;

    s_ui.dyn.cpu_usage = value_label(page, 64, 126, 66, &lv_font_montserrat_20,
                                     COL_VALUE_HOT, lv_color_hex(0x0E2246));

    s_ui.dyn.gpu_usage = value_label(page, 300, 126, 66, &lv_font_montserrat_20,
                                     COL_VALUE_HOT, lv_color_hex(0x0D2D3A));

    s_ui.dyn.cpu_temp = value_label(page, 158, 128, 58, &lv_font_montserrat_20,
                                    COL_VALUE_HOT, lv_color_hex(0x10243A));

    s_ui.dyn.gpu_temp = value_label(page, 389, 128, 58, &lv_font_montserrat_20,
                                    COL_VALUE, lv_color_hex(0x10263C));

    s_ui.dyn.cpu_freq = value_label(page, 84, 264, 72, &lv_font_montserrat_18,
                                    COL_VALUE, lv_color_hex(0x10233D));

    s_ui.dyn.board_voltage = value_label(page, 238, 264, 66, &lv_font_montserrat_18,
                                         lv_color_hex(0x7BFF95), lv_color_hex(0x102A35));

    s_ui.dyn.fan_load = value_label(page, 394, 264, 48, &lv_font_montserrat_18,
                                    lv_color_hex(0xFFD090), lv_color_hex(0x221E1A));

    s_ui.dyn.monitor_time = value_label(page, 390, 20, 72, &lv_font_montserrat_14,
                                        COL_VALUE_HOT, lv_color_hex(0x101C2B));
}

static void create_sensor_overlays(void)
{
    const ref_page_t page = UI_ASSET_PAGE_SENSOR;

    s_ui.dyn.rear_temp = value_label(page, 72, 123, 64, &lv_font_montserrat_20,
                                     COL_VALUE_HOT, lv_color_hex(0x112A46));

    s_ui.dyn.front_temp = value_label(page, 226, 123, 64, &lv_font_montserrat_20,
                                      COL_VALUE_HOT, lv_color_hex(0x112A46));

    s_ui.dyn.humidity = value_label(page, 374, 123, 68, &lv_font_montserrat_20,
                                    COL_VALUE, lv_color_hex(0x112A46));

    s_ui.dyn.sensor_time = value_label(page, 390, 20, 72, &lv_font_montserrat_14,
                                       COL_VALUE_HOT, lv_color_hex(0x0D1D33));
}

static void create_comm_overlays(void)
{
    const ref_page_t page = UI_ASSET_PAGE_COMM;

    s_ui.dyn.w800_rssi = value_label(page, 42, 105, 72, &lv_font_montserrat_12,
                                     lv_color_hex(0xE9FFF7), lv_color_hex(0x20734D));

    s_ui.dyn.modbus_stations = value_label(page, 186, 105, 34, &lv_font_montserrat_12,
                                           lv_color_hex(0xFFF2DF), lv_color_hex(0x7A4A20));

    s_ui.dyn.mqtt_qos = value_label(page, 330, 105, 48, &lv_font_montserrat_12,
                                    lv_color_hex(0xFFE9F8), lv_color_hex(0x813B62));

    s_ui.dyn.msg_rate = value_label(page, 44, 173, 92, &lv_font_montserrat_20,
                                    COL_VALUE_HOT, lv_color_hex(0x122D38));

    s_ui.dyn.msg_processed = value_label(page, 362, 174, 70, &lv_font_montserrat_18,
                                         lv_color_hex(0xF2FAFF), lv_color_hex(0x162C3A));

    s_ui.dyn.msg_dropped = value_label(page, 362, 236, 52, &lv_font_montserrat_18,
                                       lv_color_hex(0xFF9E36), lv_color_hex(0x162C3A));

    s_ui.dyn.comm_time = value_label(page, 360, 34, 76, &lv_font_montserrat_12,
                                     COL_VALUE_HOT, lv_color_hex(0x111B27));
}

static void create_dynamic_overlays(void)
{
    create_monitor_overlays();
    create_sensor_overlays();
    create_comm_overlays();
}

static void create_page_layer(void)
{
    lv_obj_t *left;
    lv_obj_t *right;

    s_ui.page_layer = rect(s_ui.screen, 0, 0, UI_W, UI_H, COL_BLACK, LV_OPA_COVER);
    s_ui.page_img = lv_image_create(s_ui.page_layer);
    lv_obj_set_pos(s_ui.page_img, 0, 0);

    s_ui.fallback_label = label(s_ui.page_layer, "UI ASSET\nnot ready", 0, 132,
                                &lv_font_montserrat_20, COL_VALUE_HOT);
    lv_obj_set_width(s_ui.fallback_label, UI_W);
    lv_obj_set_style_text_align(s_ui.fallback_label, LV_TEXT_ALIGN_CENTER, 0);

    create_dynamic_overlays();
    set_page(UI_ASSET_PAGE_BRAND);
    lv_obj_add_flag(s_ui.page_layer, LV_OBJ_FLAG_HIDDEN);

    left = rect(s_ui.page_layer, 0, 0, UI_W / 2, UI_H, COL_BLACK, LV_OPA_TRANSP);
    right = rect(s_ui.page_layer, UI_W / 2, 0, UI_W / 2, UI_H, COL_BLACK, LV_OPA_TRANSP);
    lv_obj_add_flag(left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(left, switch_zone_cb, LV_EVENT_CLICKED, (void *)0U);
    lv_obj_add_event_cb(right, switch_zone_cb, LV_EVENT_CLICKED, (void *)1U);
}

static void show_pages_cb(lv_timer_t *timer)
{
    lv_obj_add_flag(s_ui.boot_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ui.page_layer, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(auto_page_cb, AUTO_PAGE_PERIOD_MS, NULL);
    lv_timer_delete(timer);
}

void sim_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));

    s_ui.screen = lv_screen_active();
    lv_obj_remove_style_all(s_ui.screen);
    lv_obj_set_style_bg_color(s_ui.screen, COL_BLACK, 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    create_boot_layer();
    create_page_layer();
    sim_ui_update();
    start_boot_animation();

    lv_timer_create(show_pages_cb, 1900U, NULL);
}

void sim_ui_update(void)
{
    const sim_data_t *d = sim_data_get();
    char buf[24];

    if(d == NULL)
        return;

    refresh_asset_binding_if_needed();

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", d->hour, d->minute, d->second);
    set_label(s_ui.dyn.monitor_time, buf);
    set_label(s_ui.dyn.sensor_time, buf);
    set_label(s_ui.dyn.comm_time, buf);

    snprintf(buf, sizeof(buf), "%u%%", (unsigned)d->cpu_usage);
    set_label(s_ui.dyn.cpu_usage, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)d->gpu_usage);
    set_label(s_ui.dyn.gpu_usage, buf);
    snprintf(buf, sizeof(buf), "%.0fC", (double)d->cpu_temperature);
    set_label(s_ui.dyn.cpu_temp, buf);
    snprintf(buf, sizeof(buf), "%.0fC", (double)d->gpu_temperature);
    set_label(s_ui.dyn.gpu_temp, buf);
    snprintf(buf, sizeof(buf), "%u.%uG",
             (unsigned)(d->cpu_freq_mhz / 1000U),
             (unsigned)((d->cpu_freq_mhz % 1000U) / 100U));
    set_label(s_ui.dyn.cpu_freq, buf);
    snprintf(buf, sizeof(buf), "%u.%uV",
             (unsigned)(d->board_voltage_mv / 1000U),
             (unsigned)((d->board_voltage_mv % 1000U) / 100U));
    set_label(s_ui.dyn.board_voltage, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)d->fan_load);
    set_label(s_ui.dyn.fan_load, buf);

    snprintf(buf, sizeof(buf), "%.0fC", (double)d->rear_temperature);
    set_label(s_ui.dyn.rear_temp, buf);
    snprintf(buf, sizeof(buf), "%.0fC", (double)d->front_temperature);
    set_label(s_ui.dyn.front_temp, buf);
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)d->humidity);
    set_label(s_ui.dyn.humidity, buf);

    snprintf(buf, sizeof(buf), "%ddB", (int)d->w800_at.rssi);
    set_label(s_ui.dyn.w800_rssi, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)d->modbus.stations);
    set_label(s_ui.dyn.modbus_stations, buf);
    snprintf(buf, sizeof(buf), "QOS%u", (unsigned)d->mqtt.qos);
    set_label(s_ui.dyn.mqtt_qos, buf);
    snprintf(buf, sizeof(buf), "%lu/s", (unsigned long)d->msg_per_sec);
    set_label(s_ui.dyn.msg_rate, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)d->msg_processed);
    set_label(s_ui.dyn.msg_processed, buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)d->msg_dropped);
    set_label(s_ui.dyn.msg_dropped, buf);
}
