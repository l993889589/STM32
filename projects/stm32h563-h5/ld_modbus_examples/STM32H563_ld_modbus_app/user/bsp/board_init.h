/**
 * @file board_init.h
 * @brief Board initialization interface.
 */

#ifndef BOARD_INIT_H
#define BOARD_INIT_H

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Drive board-controlled outputs to documented safe levels before device use.
 * @return BSP status; BSP_STATUS_OK when all safe states are applied.
 */
bsp_status_t board_early_safe_gpio_init(void);
/**
 * Initialize board health, clock validation, safe GPIO, and required devices.
 * @return BSP status; repeated successful initialization returns BSP_STATUS_ALREADY_INITIALIZED.
 */
bsp_status_t board_init(void);

#ifdef __cplusplus
}
#endif

#endif
