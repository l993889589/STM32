/**
 * @file bsp_health.c
 * @brief Reset diagnostics and optional independent-watchdog policy.
 */

#include "bsp_health.h"

#include "bsp_reset.h"
#include "stm32f4xx_hal.h"

#define BSP_IWDG_ENABLE    1U
#define BSP_IWDG_PRESCALER IWDG_PRESCALER_64
#define BSP_IWDG_RELOAD    1250U

static bsp_health_diagnostics_t health_diagnostics;
#if BSP_IWDG_ENABLE
static IWDG_HandleTypeDef watchdog;
#endif

/** @brief Capture reset state before clearing flags and configure IWDG. */
bsp_status_t bsp_health_init(uint32_t required_services)
{
    bsp_status_t status;

    if(required_services == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    status = bsp_reset_get_causes(&health_diagnostics.reset_cause);
    if(status != BSP_STATUS_OK)
        return status;
    health_diagnostics.required_services = required_services;
    health_diagnostics.service_heartbeats = 0U;
    health_diagnostics.watchdog_feeds = 0U;
    health_diagnostics.watchdog_misses = 0U;
    bsp_reset_clear_causes();

#if BSP_IWDG_ENABLE
    watchdog.Instance = IWDG;
    watchdog.Init.Prescaler = BSP_IWDG_PRESCALER;
    watchdog.Init.Reload = BSP_IWDG_RELOAD;
    if(HAL_IWDG_Init(&watchdog) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
#endif
    return BSP_STATUS_OK;
}

/** @brief Mark one or more supervised services alive. */
void bsp_health_heartbeat(uint32_t service_mask)
{
    uint32_t state = __get_PRIMASK();

    __disable_irq();
    health_diagnostics.service_heartbeats |= service_mask;
    __set_PRIMASK(state);
}

/** @brief Evaluate service progress and conditionally feed IWDG. */
void bsp_health_poll(void)
{
#if BSP_IWDG_ENABLE
    uint32_t heartbeats;
    uint32_t state = __get_PRIMASK();

    /*
     * Close one supervision window atomically. Every required service must
     * demonstrate fresh progress again before the next watchdog feed.
     */
    __disable_irq();
    heartbeats = health_diagnostics.service_heartbeats;
    health_diagnostics.service_heartbeats = 0U;
    __set_PRIMASK(state);

    if((heartbeats &
        health_diagnostics.required_services) ==
       health_diagnostics.required_services)
    {
        if(HAL_IWDG_Refresh(&watchdog) == HAL_OK)
            health_diagnostics.watchdog_feeds++;
    }
    else
    {
        health_diagnostics.watchdog_misses++;
    }
#endif
}

/** @brief Return the static health diagnostic record. */
const bsp_health_diagnostics_t *bsp_health_diagnostics(void)
{
    return &health_diagnostics;
}
