/**
 * @file bsp_diagnostics.h
 * @brief BSP diagnostic data and update interface.
 */

#ifndef BSP_DIAGNOSTICS_H
#define BSP_DIAGNOSTICS_H

#include <stdint.h>

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BSP_FATAL_STAGE_NONE = 0,
    BSP_FATAL_STAGE_HAL,
    BSP_FATAL_STAGE_SAFE_GPIO,
    BSP_FATAL_STAGE_CLOCK,
    BSP_FATAL_STAGE_MEMORY,
    BSP_FATAL_STAGE_TIME,
    BSP_FATAL_STAGE_BOARD,
    BSP_FATAL_STAGE_RUNTIME,
    BSP_FATAL_STAGE_FAULT
} bsp_fatal_stage_t;

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t reset_flags;
    uint32_t boot_count;
    bsp_fatal_stage_t last_fatal_stage;
    bsp_status_t last_fatal_status;
    uint32_t clock_failure_count;
    uint32_t fault_count;
    uint32_t uart_overflow_count;
    uint32_t uart_recovery_count;
    uint32_t spi_timeout_count;
} bsp_diagnostics_t;

/**
 * Initialize diagnostic state for the current boot.
 * @param reset_flags Raw RCC reset-cause flags captured before clearing.
 */
void bsp_diagnostics_init(uint32_t reset_flags);
/**
 * Record the most recent fatal initialization or runtime failure.
 * @param stage Initialization/runtime stage that failed.
 * @param status BSP error associated with the failure.
 */
void bsp_diagnostics_record_fatal(bsp_fatal_stage_t stage, bsp_status_t status);
/**
 * Increment the clock-validation failure counter.
 */
void bsp_diagnostics_increment_clock_failure(void);
/**
 * Increment the processor-fault counter.
 */
void bsp_diagnostics_increment_fault(void);
/**
 * Return a read-only view of current BSP diagnostics.
 * @return Pointer to static BSP-owned diagnostic data.
 */
const bsp_diagnostics_t *bsp_diagnostics_get(void);

#ifdef __cplusplus
}
#endif

#endif
