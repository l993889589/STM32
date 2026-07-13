/**
 * @file bsp_stop.h
 * @brief Unrecoverable-error classification and safe-stop interface.
 */

#ifndef BSP_STOP_H
#define BSP_STOP_H

#include "bsp_health.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Record an unrecoverable error, mask interrupts, and enter the safe wait loop.
 * @param stage Stage at which the unrecoverable error occurred.
 * @param status BSP status that caused the stop.
 * @note This function never returns and is not ISR-safe as a recovery API.
 */
void bsp_stop_on_error(bsp_error_stage_t stage, bsp_status_t status);

#ifdef __cplusplus
}
#endif

#endif
