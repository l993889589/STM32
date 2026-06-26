#ifndef BSP_LCD_H
#define BSP_LCD_H

#include <stdint.h>
#include <stddef.h>

#include "main.h"

#define BSP_LCD_WIDTH              480U
#define BSP_LCD_HEIGHT             320U

#define BSP_LCD_BACKLIGHT_PORT     GPIOB
#define BSP_LCD_BACKLIGHT_PIN      GPIO_PIN_11
#define BSP_LCD_RESET_PORT         GPIOB
#define BSP_LCD_RESET_PIN          GPIO_PIN_4
#define BSP_LCD_DC_PORT            GPIOD
#define BSP_LCD_DC_PIN             GPIO_PIN_12
#define BSP_LCD_CS_PORT            GPIOD
#define BSP_LCD_CS_PIN             GPIO_PIN_11

#define BSP_LCD_TOUCH_RESET_PORT   GPIOB
#define BSP_LCD_TOUCH_RESET_PIN    GPIO_PIN_14
#define BSP_LCD_TOUCH_INT_PORT     GPIOB
#define BSP_LCD_TOUCH_INT_PIN      GPIO_PIN_15

#define BSP_LCD_COLOR_BLACK        0x0000U
#define BSP_LCD_COLOR_WHITE        0xFFFFU
#define BSP_LCD_COLOR_RED          0xF800U
#define BSP_LCD_COLOR_GREEN        0x07E0U
#define BSP_LCD_COLOR_BLUE         0x001FU

int bsp_lcd_init(void);
void bsp_lcd_backlight_on(void);
void bsp_lcd_backlight_off(void);
void bsp_lcd_reset(void);
int bsp_lcd_set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
int bsp_lcd_fill_color(uint16_t rgb565);
int bsp_lcd_draw_rgb565(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        const uint16_t *pixels);

#endif /* BSP_LCD_H */
