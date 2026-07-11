/**
 * @file bsp_health.c
 * @brief Persistent-in-RAM BSP health counters and last-error state.
 */

#include "bsp_health.h"

#define BSP_HEALTH_MAGIC          (0x42535044UL)
#define BSP_HEALTH_SCHEMA_VERSION (1UL)

static bsp_health_t bsp_health;

/**
 * @brief Implement bsp_health_init() as documented by its interface contract.
 */
void bsp_health_init(uint32_t reset_flags)
{
    if((bsp_health.magic != BSP_HEALTH_MAGIC) ||
       (bsp_health.schema_version != BSP_HEALTH_SCHEMA_VERSION))
    {
        bsp_health = (bsp_health_t){0};
        bsp_health.magic = BSP_HEALTH_MAGIC;
        bsp_health.schema_version = BSP_HEALTH_SCHEMA_VERSION;
    }

    bsp_health.reset_flags = reset_flags;
    bsp_health.boot_count++;
}

/**
 * @brief Implement bsp_health_record_error() as documented by its interface contract.
 */
void bsp_health_record_error(bsp_error_stage_t stage, bsp_status_t status)
{
    bsp_health.last_error_stage = stage;
    bsp_health.last_error_status = status;
}

/**
 * @brief Implement bsp_health_increment_clock_failure() as documented by its interface contract.
 */
void bsp_health_increment_clock_failure(void)
{
    bsp_health.clock_failure_count++;
}

/**
 * @brief Implement bsp_health_increment_cpu_fault() as documented by its interface contract.
 */
void bsp_health_increment_cpu_fault(void)
{
    bsp_health.cpu_fault_count++;
}

/**
 * @brief Implement bsp_health_get() as documented by its interface contract.
 */
const bsp_health_t *bsp_health_get(void)
{
    return &bsp_health;
}
