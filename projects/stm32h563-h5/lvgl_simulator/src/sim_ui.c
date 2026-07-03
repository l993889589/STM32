#include "sim_ui.h"
#include <stdint.h>
#include <string.h>

extern const lv_image_dsc_t boot_eye_img;

#define COL_BLACK       lv_color_hex(0x000000)
#define COL_FLASH       lv_color_hex(0xB640FF)
#define COL_TEXT        lv_color_hex(0xEBD6FF)
#define EYE_CENTER_Y    160

typedef struct {
    lv_obj_t *image;
    lv_obj_t *top_lid;
    lv_obj_t *bottom_lid;
    lv_obj_t *flash;
    lv_obj_t *title;
} boot_ui_t;

static boot_ui_t s_ui;

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

static void set_eye_gap(int32_t gap)
{
    int32_t top_h = EYE_CENTER_Y - (gap / 2);
    int32_t bottom_y = EYE_CENTER_Y + (gap / 2);

    if(top_h < 0) {
        top_h = 0;
    }
    if(bottom_y > 320) {
        bottom_y = 320;
    }

    lv_obj_set_height(s_ui.top_lid, top_h);
    lv_obj_set_y(s_ui.bottom_lid, bottom_y);
    lv_obj_set_height(s_ui.bottom_lid, 320 - bottom_y);
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

static void title_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void start_boot_animation(void)
{
    set_eye_gap(2);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, NULL);
    lv_anim_set_values(&a, 2, 102);
    lv_anim_set_delay(&a, 260);
    lv_anim_set_time(&a, 820);
    lv_anim_set_exec_cb(&a, eye_gap_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ui.flash);
    lv_anim_set_values(&a, 0, 95);
    lv_anim_set_delay(&a, 720);
    lv_anim_set_time(&a, 90);
    lv_anim_set_playback_time(&a, 360);
    lv_anim_set_exec_cb(&a, flash_cb);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ui.title);
    lv_anim_set_values(&a, 0, 210);
    lv_anim_set_delay(&a, 1180);
    lv_anim_set_time(&a, 720);
    lv_anim_set_exec_cb(&a, title_cb);
    lv_anim_start(&a);

}

void sim_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));

    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, COL_BLACK, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.image = lv_image_create(screen);
    lv_image_set_src(s_ui.image, &boot_eye_img);
    lv_obj_set_pos(s_ui.image, 0, 0);

    s_ui.flash = rect(screen, 0, 0, 480, 320, COL_FLASH, LV_OPA_TRANSP);

    s_ui.top_lid = rect(screen, 0, 0, 480, EYE_CENTER_Y, COL_BLACK, LV_OPA_COVER);
    s_ui.bottom_lid = rect(screen, 0, EYE_CENTER_Y, 480, 320 - EYE_CENTER_Y,
                           COL_BLACK, LV_OPA_COVER);

    s_ui.title = lv_label_create(screen);
    lv_label_set_text(s_ui.title, "LEDUO CORE AWAKENING");
    lv_obj_set_style_text_font(s_ui.title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.title, COL_TEXT, 0);
    lv_obj_set_style_text_opa(s_ui.title, LV_OPA_TRANSP, 0);
    lv_obj_align(s_ui.title, LV_ALIGN_BOTTOM_MID, 0, -22);

    start_boot_animation();
}

void sim_ui_update(void)
{
    /* Boot animation is driven by LVGL animations. */
}
