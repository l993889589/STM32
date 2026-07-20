/**
 * @file bsp_health.h
 * @brief Reset diagnostics, service heartbeats, and watchdog supervision.
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

/**
 * @brief Capture reset diagnostics and initialize the watchdog policy.
 * @param required_services Nonzero bit mask required in every feed window.
 * @return BSP_STATUS_OK or an initialization/argument error.
 */
bsp_status_t bsp_health_init(uint32_t required_services);

/**
 * @brief Report one or more live services to the watchdog supervisor.
 * @param service_mask One or more application-owned service bits.
 * @note Safe from task and interrupt context; bits are consumed by poll().
 */
void bsp_health_heartbeat(uint32_t service_mask);

/** @brief Feed the watchdog only when all required services report alive. */
void bsp_health_poll(void);

/** @brief Return the live health diagnostic snapshot. */
const bsp_health_diagnostics_t *bsp_health_diagnostics(void);

#endif /* BSP_HEALTH_H */
