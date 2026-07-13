/**
 * @file mcu_clock.c
 * @brief STM32H5 clock validation and frequency-query implementation.
 */

#include "bsp_clock.h"

#include <stdbool.h>

#include "bsp_health.h"
#include "stm32h5xx_hal.h"

static bool bsp_clock_is_initialized;
static bsp_clock_snapshot_t bsp_clock_snapshot;

/**
 * @brief Validate PLL divisors and the expected physical clock result.
 */
static bool bsp_clock_config_is_valid(const bsp_clock_config_t *config)
{
    uint64_t expected_hz;

    if((config == NULL) || (config->hse_frequency_hz != HSE_VALUE) ||
       (config->pll1_m == 0U) || (config->pll1_n == 0U) ||
       (config->pll1_p == 0U) || (config->pll1_q == 0U) ||
       (config->pll1_r == 0U))
    {
        return false;
    }

    expected_hz = ((uint64_t)config->hse_frequency_hz * config->pll1_n) /
                  ((uint64_t)config->pll1_m * config->pll1_p);

    return expected_hz == config->expected_sysclk_hz;
}

/**
 * @brief Capture current RCC frequencies into BSP-owned health state.
 */
static void bsp_clock_capture_snapshot(void)
{
    bsp_clock_snapshot.sysclk_hz = HAL_RCC_GetSysClockFreq();
    bsp_clock_snapshot.hclk_hz = HAL_RCC_GetHCLKFreq();
    bsp_clock_snapshot.pclk1_hz = HAL_RCC_GetPCLK1Freq();
    bsp_clock_snapshot.pclk2_hz = HAL_RCC_GetPCLK2Freq();
    bsp_clock_snapshot.pclk3_hz = HAL_RCC_GetPCLK3Freq();
}

/**
 * @brief Implement bsp_clock_init() as documented by its interface contract.
 */
bsp_status_t bsp_clock_init(const bsp_clock_config_t *config)
{
    if(!bsp_clock_config_is_valid(config))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(bsp_clock_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    SystemCoreClockUpdate();
    bsp_clock_capture_snapshot();

    if((__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_PLLCLK) ||
       (__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) == 0U) ||
       (bsp_clock_snapshot.sysclk_hz != config->expected_sysclk_hz))
    {
        bsp_health_increment_clock_failure();
        return BSP_STATUS_IO_ERROR;
    }

    bsp_clock_is_initialized = true;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_clock_get_hz() as documented by its interface contract.
 */
bsp_status_t bsp_clock_get_hz(bsp_clock_id_t clock_id, uint32_t *frequency_hz)
{
    RCC_ClkInitTypeDef clocks = {0};
    uint32_t flash_latency;

    if(frequency_hz == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(!bsp_clock_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    switch(clock_id)
    {
        case BSP_CLOCK_SYSCLK:
            *frequency_hz = bsp_clock_snapshot.sysclk_hz;
            break;
        case BSP_CLOCK_HCLK:
            *frequency_hz = bsp_clock_snapshot.hclk_hz;
            break;
        case BSP_CLOCK_PCLK1:
            *frequency_hz = bsp_clock_snapshot.pclk1_hz;
            break;
        case BSP_CLOCK_TIMER_APB1:
            HAL_RCC_GetClockConfig(&clocks, &flash_latency);
            *frequency_hz = clocks.APB1CLKDivider == RCC_HCLK_DIV1 ?
                            bsp_clock_snapshot.pclk1_hz :
                            bsp_clock_snapshot.pclk1_hz * 2U;
            break;
        case BSP_CLOCK_PCLK2:
            *frequency_hz = bsp_clock_snapshot.pclk2_hz;
            break;
        case BSP_CLOCK_TIMER_APB2:
            HAL_RCC_GetClockConfig(&clocks, &flash_latency);
            *frequency_hz = clocks.APB2CLKDivider == RCC_HCLK_DIV1 ?
                            bsp_clock_snapshot.pclk2_hz :
                            bsp_clock_snapshot.pclk2_hz * 2U;
            break;
        case BSP_CLOCK_PCLK3:
            *frequency_hz = bsp_clock_snapshot.pclk3_hz;
            break;
        default:
            return BSP_STATUS_INVALID_ARGUMENT;
    }

    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_clock_get_snapshot() as documented by its interface contract.
 */
const bsp_clock_snapshot_t *bsp_clock_get_snapshot(void)
{
    return &bsp_clock_snapshot;
}
