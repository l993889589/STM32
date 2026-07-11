/**
 * @file bsp_clock.h
 * @brief Clock validation and frequency-query interface.
 */

#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdint.h>

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BSP_CLOCK_SYSCLK = 0,
    BSP_CLOCK_HCLK,
    BSP_CLOCK_PCLK1,
    BSP_CLOCK_PCLK2,
    BSP_CLOCK_PCLK3,
    BSP_CLOCK_TIMER_APB1,
    BSP_CLOCK_TIMER_APB2
} bsp_clock_id_t;

typedef struct
{
    uint32_t hse_frequency_hz;
    uint32_t pll1_m;
    uint32_t pll1_n;
    uint32_t pll1_p;
    uint32_t pll1_q;
    uint32_t pll1_r;
    uint32_t expected_sysclk_hz;
} bsp_clock_config_t;

typedef struct
{
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
    uint32_t pclk3_hz;
} bsp_clock_snapshot_t;

/**
 * Validate the already-configured clock tree and capture a frequency snapshot.
 * @param config Expected HSE, PLL, and SYSCLK configuration.
 * @return BSP_STATUS_OK, BSP_STATUS_ALREADY_INITIALIZED, or a validation error.
 */
bsp_status_t bsp_clock_init(const bsp_clock_config_t *config);
/**
 * Read a validated clock-domain frequency.
 * @param clock_id Logical clock domain to query.
 * @param frequency_hz Receives the frequency in hertz.
 * @return BSP status; BSP_STATUS_NOT_READY before clock validation.
 */
bsp_status_t bsp_clock_get_hz(bsp_clock_id_t clock_id, uint32_t *frequency_hz);
/**
 * Return the read-only captured clock snapshot.
 * @return Pointer to static BSP-owned clock data.
 */
const bsp_clock_snapshot_t *bsp_clock_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif
