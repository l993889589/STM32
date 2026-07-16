/**
 * @file bsp_clock.h
 * @brief Clock validation and physical frequency query interface.
 */

#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Logical clock domains consumed by portable peripheral solvers. */
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

/** @brief Expected board oscillator and PLL1 configuration. */
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

/** @brief Read-only snapshot captured after system clock configuration. */
typedef struct
{
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
    uint32_t pclk3_hz;
} bsp_clock_snapshot_t;

/**
 * @brief Configure the board's 250 MHz HSE/PLL clock tree.
 * @return BSP status after oscillator, bus, USB clock recovery, and flash setup.
 * @note Call at startup and again immediately after returning from Stop mode.
 */
bsp_status_t bsp_clock_configure_system(void);

/**
 * @brief Validate the configured clock tree and capture its frequencies.
 * @param config Expected physical oscillator and PLL values.
 * @return BSP status; no clock registers are changed by this function.
 */
bsp_status_t bsp_clock_init(const bsp_clock_config_t *config);

/**
 * @brief Read a validated clock-domain frequency.
 * @param clock_id Logical clock domain.
 * @param frequency_hz Destination for the frequency in hertz.
 * @return BSP status.
 */
bsp_status_t bsp_clock_get_hz(bsp_clock_id_t clock_id, uint32_t *frequency_hz);

/**
 * @brief Return the captured read-only clock snapshot.
 * @return Pointer to static BSP-owned clock data.
 */
const bsp_clock_snapshot_t *bsp_clock_get_snapshot(void);

#endif /* BSP_CLOCK_H */
