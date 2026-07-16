/**
 * @file lv_port_disp.c
 * @brief LVGL display flush adapter for the board-owned LCD driver.
 */

#include "lv_port_disp.h"

#include <stdint.h>

#include "bsp_lcd.h"

#define LV_PORT_DISP_BUF_LINES  32U

static lv_display_t *s_display;
static LV_ATTRIBUTE_MEM_ALIGN lv_color_t s_draw_buf_1[BSP_LCD_WIDTH * LV_PORT_DISP_BUF_LINES];
static LV_ATTRIBUTE_MEM_ALIGN lv_color_t s_draw_buf_2[BSP_LCD_WIDTH * LV_PORT_DISP_BUF_LINES];

static void lv_port_disp_flush_ready(void *arg)
{
    lv_display_flush_ready((lv_display_t *)arg);
}

static void lv_port_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t orig_x1 = area->x1;
    const int32_t orig_y1 = area->y1;
    const int32_t orig_x2 = area->x2;
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    uint16_t orig_width;

    if(x2 < 0 || y2 < 0 || x1 >= (int32_t)BSP_LCD_WIDTH || y1 >= (int32_t)BSP_LCD_HEIGHT)
    {
        lv_display_flush_ready(disp);
        return;
    }

    if(x1 < 0)
    {
        x1 = 0;
    }
    if(y1 < 0)
    {
        y1 = 0;
    }
    if(x2 >= (int32_t)BSP_LCD_WIDTH)
    {
        x2 = (int32_t)BSP_LCD_WIDTH - 1;
    }
    if(y2 >= (int32_t)BSP_LCD_HEIGHT)
    {
        y2 = (int32_t)BSP_LCD_HEIGHT - 1;
    }

    uint16_t width = (uint16_t)(x2 - x1 + 1);
    uint16_t height = (uint16_t)(y2 - y1 + 1);
    orig_width = (uint16_t)(orig_x2 - orig_x1 + 1);

    if((x1 == orig_x1) && (y1 == orig_y1) && (x2 == orig_x2))
    {
        if(bsp_lcd_draw_rgb565_dma((uint16_t)x1,
                                   (uint16_t)y1,
                                   width,
                                   height,
                                   (const uint16_t *)px_map) == 0)
        {
            return;
        }

        (void)bsp_lcd_draw_rgb565((uint16_t)x1,
                                  (uint16_t)y1,
                                  width,
                                  height,
                                  (const uint16_t *)px_map);
    }
    else
    {
        const uint16_t *src = (const uint16_t *)px_map;
        int32_t row;

        for(row = 0; row < height; row++)
        {
            uint32_t src_offset = (uint32_t)(y1 - orig_y1 + row) * orig_width +
                                  (uint32_t)(x1 - orig_x1);
            (void)bsp_lcd_draw_rgb565((uint16_t)x1,
                                      (uint16_t)(y1 + row),
                                      width,
                                      1U,
                                      &src[src_offset]);
        }
    }

    lv_display_flush_ready(disp);
}

lv_display_t *lv_port_disp_init(void)
{
    if(s_display != NULL)
    {
        return s_display;
    }

    s_display = lv_display_create((int32_t)BSP_LCD_WIDTH, (int32_t)BSP_LCD_HEIGHT);
    if(s_display == NULL)
    {
        return NULL;
    }

    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, lv_port_disp_flush);
    bsp_lcd_set_flush_complete_callback(lv_port_disp_flush_ready, s_display);
    lv_display_set_buffers(s_display,
                           s_draw_buf_1,
                           s_draw_buf_2,
                           sizeof(s_draw_buf_1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    return s_display;
}
