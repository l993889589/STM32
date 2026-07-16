/**
 * @file bsp_health.h
 * @brief BSP health monitoring data and watchdog service interfaces.
 */

#ifndef BSP_HEALTH_H
#define BSP_HEALTH_H

#include <stdint.h>

#include "bsp_status.h"

typedef struct
{
    uint32_t reset_cause;
    uint32_t required_services;
    uint32_t service_heartbeats;
    uint32_t watchdog_feeds;
    uint32_t watchdog_misses;
} bsp_health_diagnostics_t;

/** @brief Initialize reset diagnostics and optional watchdog policy. */
bsp_status_t bsp_health_init(uint32_t required_services);
/** @brief Report one or more live services to the watchdog supervisor. */
void bsp_health_heartbeat(uint32_t service_mask);
/** @brief Evaluate heartbeats and conditionally feed the watchdog. */
void bsp_health_poll(void);
/** @brief Return the live health diagnostic snapshot. */
const bsp_health_diagnostics_t *bsp_health_diagnostics(void);

#endif
