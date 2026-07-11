/**
 * @file st7796.h
 * @brief ST7796 480x320 SPI display initialization and bounded RGB565 transfer interface.
 */

#ifndef ST7796_H
#define ST7796_H

#include <stdint.h>

#include "bsp_status.h"

#define ST7796_WIDTH  (480U)
#define ST7796_HEIGHT (320U)

/**
 * @brief Initialize SPI2, reset the display, configure RGB565 landscape mode, and leave backlight off.
 * @return BSP status.
 * @note Performs bounded delays and must run outside interrupt context.
 */
bsp_status_t st7796_init(void);

/**
 * @brief Set backlight duty using the timer solver.
 * @param duty_permille Brightness from 0 to 1000 permille.
 * @return BSP status.
 */
bsp_status_t st7796_set_backlight(uint16_t duty_permille);

/**
 * @brief Select the active display-memory window.
 * @param x Left coordinate.
 * @param y Top coordinate.
 * @param width Window width in pixels.
 * @param height Window height in pixels.
 * @return BSP status.
 */
bsp_status_t st7796_set_window(uint16_t x,
                                   uint16_t y,
                                   uint16_t width,
                                   uint16_t height);

/**
 * @brief Write RGB565 pixels to the previously selected window.
 * @param pixels Caller-owned RGB565 pixels.
 * @param pixel_count Number of pixels.
 * @param timeout_ms Maximum time for each bounded SPI chunk.
 * @return BSP status.
 */
bsp_status_t st7796_write_pixels(const uint16_t *pixels,
                                     uint32_t pixel_count,
                                     uint32_t timeout_ms);

/**
 * @brief Fill the previously selected window with one RGB565 color.
 * @param color RGB565 color.
 * @param pixel_count Number of pixels to write.
 * @param timeout_ms Maximum time for each bounded SPI chunk.
 * @return BSP status.
 */
bsp_status_t st7796_fill(uint16_t color,
                             uint32_t pixel_count,
                             uint32_t timeout_ms);

#endif
