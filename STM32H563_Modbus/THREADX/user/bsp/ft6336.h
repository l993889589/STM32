/**
 * @file ft6336.h
 * @brief FT6336U capacitive-touch initialization and bounded polling interface.
 */

#ifndef FT6336_H
#define FT6336_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

typedef struct
{
    uint8_t event;
    uint8_t touch_id;
    uint16_t x;
    uint16_t y;
} ft6336_point_t;

typedef struct
{
    uint8_t gesture;
    uint8_t point_count;
    ft6336_point_t points[2];
} ft6336_state_t;

typedef struct
{
    uint8_t vendor_id;
    uint8_t chip_id;
} ft6336_identity_t;

/**
 * @brief Initialize I2C1, reset the FT6336U, and read its identity registers.
 * @param identity Optional destination for vendor and chip identifiers.
 * @return BSP status.
 * @note Performs bounded reset/startup delays in task or superloop context.
 */
bsp_status_t ft6336_init(ft6336_identity_t *identity);

/**
 * @brief Poll the touch interrupt input without enabling EXTI.
 * @param is_active Receives true when the active-low interrupt is asserted.
 * @return BSP status.
 */
bsp_status_t ft6336_interrupt_is_active(bool *is_active);

/**
 * @brief Read the current gesture and up to two touch points.
 * @param state Receives a complete touch-state snapshot.
 * @return BSP status.
 */
bsp_status_t ft6336_read_state(ft6336_state_t *state);

#endif
