/**
 * @file bsp_lcd.h
 * @brief ST7796 display service API independent of STM32 handles and pins.
 */

#ifndef BSP_LCD_H
#define BSP_LCD_H

#include <stddef.h>
#include <stdint.h>

#define BSP_LCD_WIDTH       (480U)
#define BSP_LCD_HEIGHT      (320U)
#define BSP_LCD_COLOR_BLACK (0x0000U)
#define BSP_LCD_COLOR_WHITE (0xFFFFU)
#define BSP_LCD_COLOR_RED   (0xF800U)
#define BSP_LCD_COLOR_GREEN (0x07E0U)
#define BSP_LCD_COLOR_BLUE  (0x001FU)

/** @brief Display flush completion callback executed in ISR context for DMA. */
typedef void (*bsp_lcd_flush_complete_cb_t)(void *argument);

/** @brief Initialize the ST7796 display. @return Zero on success. */
int bsp_lcd_init(void);
/** @brief Set backlight PWM to full duty. */
void bsp_lcd_backlight_on(void);
/** @brief Set backlight PWM to zero duty. */
void bsp_lcd_backlight_off(void);
/** @brief Perform the bounded active-low display reset sequence. */
void bsp_lcd_reset(void);
/** @brief Set one validated display drawing window. */
int bsp_lcd_set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
/** @brief Fill the complete panel with one RGB565 color. */
int bsp_lcd_fill_color(uint16_t rgb565);
/** @brief Draw one RGB565 rectangle with bounded blocking chunks. */
int bsp_lcd_draw_rgb565(uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        const uint16_t *pixels);
/** @brief Register the ISR-context asynchronous flush completion callback. */
void bsp_lcd_set_flush_complete_callback(bsp_lcd_flush_complete_cb_t callback,
                                         void *argument);
/** @brief Start one RGB565 rectangle transfer using board-owned SPI TX DMA. */
int bsp_lcd_draw_rgb565_dma(uint16_t x,
                            uint16_t y,
                            uint16_t width,
                            uint16_t height,
                            const uint16_t *pixels);

#endif /* BSP_LCD_H */
