/*
 * Application health supervisor used to authorize OTA trial confirmation.
 *
 * Service tasks report scheduler progress, not external network/device success.
 * The OTA confirm task queries one coherent status and never confirms while a
 * required heartbeat is missing, stale, or a fatal runtime fault is latched.
 */
#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include "tx_api.h"
#include <stdint.h>

typedef enum
{
    APP_HEALTH_SERVICE_RS485 = 0,
    APP_HEALTH_SERVICE_W800 = 1,
    APP_HEALTH_SERVICE_UI = 2,
    APP_HEALTH_SERVICE_COUNT = 3
} app_health_service_t;

typedef enum
{
    APP_HEALTH_FAULT_NONE = 0,
    APP_HEALTH_FAULT_THREAD_STACK = 1,
    APP_HEALTH_FAULT_UI_INIT = 2
} app_health_fault_t;

typedef struct
{
    uint32_t required_mask;
    uint32_t seen_mask;
    uint32_t stale_mask;
    uint32_t fatal_fault;
    ULONG observation_ticks;
} app_health_status_t;

/* Initialize health tracking before required application threads start. */
void app_health_init(void);

/* Report one bounded task loop completing useful scheduler progress. */
void app_health_report(app_health_service_t service);

/* Latch a fatal runtime fault; fatal faults remain set until reset. */
void app_health_report_fault(app_health_fault_t fault);

/* Return nonzero only after the observation window and with fresh heartbeats. */
uint8_t app_health_is_ready(
    ULONG observation_window_ticks,
    ULONG stale_limit_ticks,
    app_health_status_t *status);

#endif /* APP_HEALTH_H */
