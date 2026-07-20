/**
 * @file bsp_clock.h
 * @brief CHPM clock configuration, validation, and frequency queries.
 */

#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    BSP_CLOCK_SYSCLK = 0,
    BSP_CLOCK_HCLK,
    BSP_CLOCK_PCLK1,
    BSP_CLOCK_PCLK2,
    BSP_CLOCK_TIMER_APB1,
    BSP_CLOCK_TIMER_APB2
} bsp_clock_id_t;

typedef struct
{
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
} bsp_clock_snapshot_t;

/** @brief Configure the 25 MHz HSE/PLL clock tree for 84 MHz SYSCLK. */
bsp_status_t bsp_clock_configure_system(void);

/** @brief Validate the fixed clock tree and capture its frequencies. */
bsp_status_t bsp_clock_init(void);

/** @brief Read a validated logical clock-domain frequency. */
bsp_status_t bsp_clock_get_hz(bsp_clock_id_t clock_id, uint32_t *frequency_hz);

/** @brief Return the captured read-only clock snapshot. */
const bsp_clock_snapshot_t *bsp_clock_get_snapshot(void);

#endif /* BSP_CLOCK_H */
