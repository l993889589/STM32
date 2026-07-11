/**
 * @file bsp_fatal.h
 * @brief Fatal-stage classification and terminal fault interface.
 */

#ifndef BSP_FATAL_H
#define BSP_FATAL_H

#include "bsp_diagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Record a fatal error, mask interrupts, and enter the terminal safe wait loop.
 * @param stage Stage at which the fatal error occurred.
 * @param status BSP status that caused the stop.
 * @note This function never returns and is not ISR-safe as a recovery API.
 */
void bsp_fatal_stop(bsp_fatal_stage_t stage, bsp_status_t status);

#ifdef __cplusplus
}
#endif

#endif
