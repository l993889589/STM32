/**
 * @file board_control.h
 * @brief Private logical GPIO controls for populated board devices.
 */

#ifndef BOARD_CONTROL_H
#define BOARD_CONTROL_H

#include <stdbool.h>

#include "bsp_status.h"

typedef enum
{
    BOARD_CONTROL_WIFI_BOOT = 0,
    BOARD_CONTROL_WIFI_RESET,
    BOARD_CONTROL_WIFI_WAKE,
    BOARD_CONTROL_DISPLAY_DC,
    BOARD_CONTROL_DISPLAY_RESET,
    BOARD_CONTROL_TOUCH_RESET,
    BOARD_CONTROL_TOUCH_INTERRUPT,
    BOARD_CONTROL_USB_ID,
    BOARD_CONTROL_COUNT
} board_control_role_t;

/**
 * @brief Configure board control outputs and read-only inputs in safe states.
 * @return BSP status; repeated initialization returns BSP_STATUS_ALREADY_INITIALIZED.
 */
bsp_status_t board_control_init(void);

/**
 * @brief Set the logical active state of an output control.
 * @param role Logical control role.
 * @param is_active True for the role's active state; polarity is applied internally.
 * @return BSP status; read-only roles return BSP_STATUS_NOT_SUPPORTED.
 */
bsp_status_t board_control_write(board_control_role_t role, bool is_active);

/**
 * @brief Read the logical active state of a board control or input.
 * @param role Logical control role.
 * @param is_active Receives the polarity-corrected logical state.
 * @return BSP status.
 */
bsp_status_t board_control_read(board_control_role_t role, bool *is_active);

#endif
