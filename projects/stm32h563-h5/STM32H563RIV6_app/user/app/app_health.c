/*
 * ThreadX application health supervisor implementation.
 *
 * All shared fields are naturally aligned 32-bit values on Cortex-M33. Writers
 * only replace their own heartbeat or latch bits, so task-context access does
 * not require a mutex and cannot block service loops.
 */
#include "app_health.h"
#include <string.h>

#define APP_HEALTH_REQUIRED_MASK  ((1UL << APP_HEALTH_SERVICE_RS485) | \
                                   (1UL << APP_HEALTH_SERVICE_W800) | \
                                   (1UL << APP_HEALTH_SERVICE_UI))

static volatile ULONG health_started_at;
static volatile ULONG heartbeat_at[APP_HEALTH_SERVICE_COUNT];
static volatile uint32_t seen_mask;
static volatile uint32_t fatal_fault;

void app_health_init(void)
{
    uint32_t index;

    health_started_at = tx_time_get();
    seen_mask = 0U;
    fatal_fault = (uint32_t)APP_HEALTH_FAULT_NONE;
    for(index = 0U; index < (uint32_t)APP_HEALTH_SERVICE_COUNT; index++)
    {
        heartbeat_at[index] = health_started_at;
    }
}

void app_health_report(app_health_service_t service)
{
    if((uint32_t)service < (uint32_t)APP_HEALTH_SERVICE_COUNT)
    {
        heartbeat_at[service] = tx_time_get();
        seen_mask |= 1UL << (uint32_t)service;
    }
}

void app_health_report_fault(app_health_fault_t fault)
{
    if(fault != APP_HEALTH_FAULT_NONE)
    {
        fatal_fault = (uint32_t)fault;
    }
}

uint8_t app_health_is_ready(
    ULONG observation_window_ticks,
    ULONG stale_limit_ticks,
    app_health_status_t *status)
{
    app_health_status_t snapshot;
    ULONG now = tx_time_get();
    uint32_t index;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.required_mask = APP_HEALTH_REQUIRED_MASK;
    snapshot.seen_mask = seen_mask;
    snapshot.fatal_fault = fatal_fault;
    snapshot.observation_ticks = now - health_started_at;

    for(index = 0U; index < (uint32_t)APP_HEALTH_SERVICE_COUNT; index++)
    {
        uint32_t bit = 1UL << index;
        if((snapshot.required_mask & bit) != 0U &&
           (snapshot.seen_mask & bit) != 0U &&
           (ULONG)(now - heartbeat_at[index]) > stale_limit_ticks)
        {
            snapshot.stale_mask |= bit;
        }
    }

    if(status != NULL)
    {
        *status = snapshot;
    }

    return (snapshot.observation_ticks >= observation_window_ticks &&
            snapshot.fatal_fault == (uint32_t)APP_HEALTH_FAULT_NONE &&
            (snapshot.seen_mask & snapshot.required_mask) == snapshot.required_mask &&
            snapshot.stale_mask == 0U) ? 1U : 0U;
}

void app_health_get_status(
    ULONG stale_limit_ticks,
    app_health_status_t *status)
{
    (void)app_health_is_ready(0U, stale_limit_ticks, status);
}
