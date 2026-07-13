/**
 * @file bsp_health.h
 * @brief BSP health data and update interface.
 */

#ifndef BSP_HEALTH_H
#define BSP_HEALTH_H

#include <stdint.h>

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BSP_ERROR_STAGE_NONE = 0,
    BSP_ERROR_STAGE_HAL,
    BSP_ERROR_STAGE_SAFE_GPIO,
    BSP_ERROR_STAGE_CLOCK,
    BSP_ERROR_STAGE_MEMORY,
    BSP_ERROR_STAGE_TIME,
    BSP_ERROR_STAGE_BOARD,
    BSP_ERROR_STAGE_RUNTIME,
    BSP_ERROR_STAGE_FAULT
} bsp_error_stage_t;

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t reset_flags;
    uint32_t boot_count;
    bsp_error_stage_t last_error_stage;
    bsp_status_t last_error_status;
    uint32_t clock_failure_count;
    uint32_t cpu_fault_count;
    uint32_t uart_overflow_count;
    uint32_t uart_recovery_count;
    uint32_t spi_timeout_count;
} bsp_health_t;

/**
 * Initialize health state for the current boot.
 * @param reset_flags Raw RCC reset-cause flags captured before clearing.
 */
void bsp_health_init(uint32_t reset_flags);
/**
 * Record the most recent unrecoverable initialization or runtime error.
 * @param stage Initialization/runtime stage that failed.
 * @param status BSP error associated with the failure.
 */
void bsp_health_record_error(bsp_error_stage_t stage, bsp_status_t status);
/**
 * Increment the clock-validation failure counter.
 */
void bsp_health_increment_clock_failure(void);
/**
 * Increment the processor-fault counter.
 */
void bsp_health_increment_cpu_fault(void);
/**
 * Return a read-only view of current BSP health.
 * @return Pointer to static BSP-owned health data.
 */
const bsp_health_t *bsp_health_get(void);

#ifdef __cplusplus
}
#endif

#endif
