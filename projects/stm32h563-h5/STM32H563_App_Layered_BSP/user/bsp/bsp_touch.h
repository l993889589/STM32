/**
 * @file bsp_touch.h
 * @brief FT6336 touch state interface independent of I2C and GPIO details.
 */

#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdint.h>

#define BSP_TOUCH_WIDTH  (480U)
#define BSP_TOUCH_HEIGHT (320U)

/** @brief One touch-controller state snapshot. */
typedef struct
{
    uint8_t present;
    uint8_t touched;
    uint8_t points;
    uint16_t x;
    uint16_t y;
    uint8_t event;
    uint8_t gesture;
    uint8_t chip_id;
    uint8_t vendor_id;
    uint8_t int_active;
} bsp_touch_state_t;

/** @brief Initialize the logical touch bus and perform its bounded reset sequence. */
int bsp_touch_init(void);
/** @brief Read one touch state snapshot. */
int bsp_touch_read(bsp_touch_state_t *state);
/** @brief Read the logical active-low touch interrupt input. */
uint8_t bsp_touch_int_active(void);

#endif /* BSP_TOUCH_H */
