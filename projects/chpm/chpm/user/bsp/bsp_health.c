/**
 * @file bsp_health.c
 * @brief Reset diagnostics, service heartbeats and optional watchdog policy.
 */

#include "bsp_health.h"

#include "board_config.h"
#include "stm32f4xx_hal.h"

static bsp_health_diagnostics_t health_diagnostics;
#if BOARD_IWDG_ENABLE
static IWDG_HandleTypeDef watchdog;
#endif

/** @brief Capture reset cause and initialize optional independent watchdog. */
bsp_status_t bsp_health_init(uint32_t required_services)
{
    health_diagnostics.reset_cause = RCC->CSR;
    health_diagnostics.required_services = required_services;
    health_diagnostics.service_heartbeats = 0U;
    health_diagnostics.watchdog_feeds = 0U;
    health_diagnostics.watchdog_misses = 0U;
    __HAL_RCC_CLEAR_RESET_FLAGS();

#if BOARD_IWDG_ENABLE
    watchdog.Instance = IWDG;
    watchdog.Init.Prescaler = BOARD_IWDG_PRESCALER;
    watchdog.Init.Reload = BOARD_IWDG_RELOAD;
    if(HAL_IWDG_Init(&watchdog) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
#endif
    return BSP_STATUS_OK;
}

/** @brief Mark one or more monitored services as alive for this period. */
void bsp_health_heartbeat(uint32_t service_mask)
{
    health_diagnostics.service_heartbeats |= service_mask;
}

/** @brief Feed the watchdog only after every required service reports alive. */
void bsp_health_poll(void)
{
#if BOARD_IWDG_ENABLE
    if((health_diagnostics.service_heartbeats & health_diagnostics.required_services) ==
       health_diagnostics.required_services)
    {
        health_diagnostics.service_heartbeats = 0U;
        if(HAL_IWDG_Refresh(&watchdog) == HAL_OK)
            health_diagnostics.watchdog_feeds++;
    }
    else
    {
        health_diagnostics.watchdog_misses++;
    }
#endif
}

/** @brief Return the live health and watchdog diagnostic counters. */
const bsp_health_diagnostics_t *bsp_health_diagnostics(void)
{
    return &health_diagnostics;
}
