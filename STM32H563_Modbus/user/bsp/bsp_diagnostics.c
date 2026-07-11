/**
 * @file bsp_diagnostics.c
 * @brief Persistent-in-RAM BSP diagnostic counters and fatal state.
 */

#include "bsp_diagnostics.h"

#define BSP_DIAGNOSTICS_MAGIC          (0x42535044UL)
#define BSP_DIAGNOSTICS_SCHEMA_VERSION (1UL)

static bsp_diagnostics_t bsp_diagnostics;

/**
 * @brief Implement bsp_diagnostics_init() as documented by its interface contract.
 */
void bsp_diagnostics_init(uint32_t reset_flags)
{
    if((bsp_diagnostics.magic != BSP_DIAGNOSTICS_MAGIC) ||
       (bsp_diagnostics.schema_version != BSP_DIAGNOSTICS_SCHEMA_VERSION))
    {
        bsp_diagnostics = (bsp_diagnostics_t){0};
        bsp_diagnostics.magic = BSP_DIAGNOSTICS_MAGIC;
        bsp_diagnostics.schema_version = BSP_DIAGNOSTICS_SCHEMA_VERSION;
    }

    bsp_diagnostics.reset_flags = reset_flags;
    bsp_diagnostics.boot_count++;
}

/**
 * @brief Implement bsp_diagnostics_record_fatal() as documented by its interface contract.
 */
void bsp_diagnostics_record_fatal(bsp_fatal_stage_t stage, bsp_status_t status)
{
    bsp_diagnostics.last_fatal_stage = stage;
    bsp_diagnostics.last_fatal_status = status;
}

/**
 * @brief Implement bsp_diagnostics_increment_clock_failure() as documented by its interface contract.
 */
void bsp_diagnostics_increment_clock_failure(void)
{
    bsp_diagnostics.clock_failure_count++;
}

/**
 * @brief Implement bsp_diagnostics_increment_fault() as documented by its interface contract.
 */
void bsp_diagnostics_increment_fault(void)
{
    bsp_diagnostics.fault_count++;
}

/**
 * @brief Implement bsp_diagnostics_get() as documented by its interface contract.
 */
const bsp_diagnostics_t *bsp_diagnostics_get(void)
{
    return &bsp_diagnostics;
}
