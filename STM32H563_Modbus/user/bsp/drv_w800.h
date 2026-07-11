/**
 * @file drv_w800.h
 * @brief W800 module boot, reset, and wake control interface.
 */

#ifndef DRV_W800_H
#define DRV_W800_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    DRV_W800_BOOT_NORMAL = 0,
    DRV_W800_BOOT_DOWNLOAD
} drv_w800_boot_mode_t;

typedef struct
{
    drv_w800_boot_mode_t boot_mode;
    uint32_t reset_hold_ms;
    uint32_t boot_settle_ms;
} drv_w800_config_t;

/**
 * @brief Apply the selected boot strap and perform a bounded W800 reset sequence.
 * @param config Boot mode and timing values in milliseconds.
 * @return BSP status.
 * @note Call from task/superloop context only; this function performs bounded delays.
 */
bsp_status_t drv_w800_init(const drv_w800_config_t *config);

/**
 * @brief Assert or release the W800 wake request.
 * @param is_requested True to request wake; false to release it.
 * @return BSP status.
 */
bsp_status_t drv_w800_set_wake(bool is_requested);

/**
 * @brief Perform another bounded W800 reset using the initialized timing.
 * @return BSP status.
 */
bsp_status_t drv_w800_reset(void);

#endif
