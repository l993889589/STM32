#include "app_ui.h"

#include "lv_port_disp.h"
#include "lvgl.h"

#define APP_UI_TICK_MS  5UL

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
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 14, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(card, 8, 0);
    lv_obj_set_style_pad_all(card, 14, 0);

    lv_obj_t *dot = lv_obj_create(card);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 38, 38);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x5D6785), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 50, 0);

    lv_obj_t *main_value = lv_label_create(card);
    lv_label_set_text(main_value, value);
    lv_obj_set_style_text_font(main_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(main_value, lv_color_hex(0x25315F), 0);
    lv_obj_align(main_value, LV_ALIGN_TOP_LEFT, 50, 22);

    lv_obj_t *hint_label = lv_label_create(card);
    lv_label_set_text(hint_label, hint);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x8A93AA), 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_LEFT, 2, 0);
}

static void app_ui_create_dashboard(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xDFF6FF), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0xFFF1D6), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *sun = lv_obj_create(screen);
    lv_obj_remove_style_all(sun);
    lv_obj_set_size(sun, 82, 82);
    lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sun, lv_color_hex(0xFFD166), 0);
    lv_obj_set_style_shadow_width(sun, 18, 0);
    lv_obj_set_style_shadow_color(sun, lv_color_hex(0xFFB703), 0);
    lv_obj_set_style_shadow_opa(sun, LV_OPA_30, 0);
    lv_obj_set_pos(sun, 372, 20);

    lv_obj_t *mascot = lv_obj_create(screen);
    lv_obj_remove_style_all(mascot);
    lv_obj_set_size(mascot, 86, 86);
    lv_obj_set_style_radius(mascot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mascot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mascot, lv_color_hex(0x9BF6FF), 0);
    lv_obj_set_style_border_width(mascot, 6, 0);
    lv_obj_set_style_border_color(mascot, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(mascot, 18, 0);
    lv_obj_set_style_shadow_opa(mascot, LV_OPA_20, 0);
    lv_obj_set_pos(mascot, 22, 24);

    lv_obj_t *face = lv_label_create(mascot);
    lv_label_set_text(face, ":)");
    lv_obj_set_style_text_font(face, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(face, lv_color_hex(0x26547C), 0);
    lv_obj_center(face);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "H563 Buddy");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x25315F), 0);
    lv_obj_set_pos(title, 126, 30);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Dashboard is online");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x65708E), 0);
    lv_obj_set_pos(subtitle, 130, 68);

    lv_obj_t *pill = lv_obj_create(screen);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 168, 34);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0xB8F2E6), 0);
    lv_obj_set_pos(pill, 130, 94);

    lv_obj_t *pill_text = lv_label_create(pill);
    lv_label_set_text(pill_text, "LCD 480x320 RGB565");
    lv_obj_set_style_text_font(pill_text, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pill_text, lv_color_hex(0x1B6B6F), 0);
    lv_obj_center(pill_text);

    app_ui_add_status_card(screen, 20, 144, 136, 72, lv_color_hex(0x06D6A0), "W800", "WiFi", "MQTT link");
    app_ui_add_status_card(screen, 172, 144, 136, 72, lv_color_hex(0xFF6B6B), "NearLink", "SLE", "UART3 AT");
    app_ui_add_status_card(screen, 324, 144, 136, 72, lv_color_hex(0x4D96FF), "RS485", "Modbus", "bus ready");

    lv_obj_t *log = lv_obj_create(screen);
    lv_obj_remove_style_all(log);
    lv_obj_set_pos(log, 20, 236);
    lv_obj_set_size(log, 440, 58);
    lv_obj_set_style_radius(log, 20, 0);
    lv_obj_set_style_bg_opa(log, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(log, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(log, 0, 0);
    lv_obj_set_style_shadow_width(log, 12, 0);
    lv_obj_set_style_shadow_opa(log, LV_OPA_20, 0);

    lv_obj_t *log_text = lv_label_create(log);
    lv_label_set_text(log_text, "Tip: UI runs as a low-priority ThreadX task.");
    lv_obj_set_style_text_font(log_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(log_text, lv_color_hex(0x5D6785), 0);
    lv_obj_center(log_text);

    lv_screen_load(screen);
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

void app_ui_task_entry(ULONG thread_input)
{
    TX_PARAMETER_NOT_USED(thread_input);

    for(;;)
    {
        lv_tick_inc(APP_UI_TICK_MS);
        (void)lv_timer_handler();
        tx_thread_sleep(APP_UI_TICK_MS);
    }
}
