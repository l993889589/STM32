/**
 * @file bsp_clock.c
 * @brief STM32H5 clock validation and frequency-query implementation.
 */

#include "bsp_clock.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32h5xx_hal.h"

static bool g_bsp_clock_initialized;
static bsp_clock_snapshot_t g_bsp_clock_snapshot;

/** @brief Capture the currently active RCC clock frequencies. */
static void bsp_clock_hw_capture_snapshot(void);

/** @brief Configure the authoritative board clock tree at boot or after Stop. */
bsp_status_t bsp_clock_configure_system(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};
    RCC_CRSInitTypeDef crs = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while(__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U)
    {
    }

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI48 |
                                RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.HSI48State = RCC_HSI48_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
    oscillator.PLL.PLLM = 2U;
    oscillator.PLL.PLLN = 40U;
    oscillator.PLL.PLLP = 2U;
    oscillator.PLL.PLLQ = 2U;
    oscillator.PLL.PLLR = 2U;
    oscillator.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
    oscillator.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
    oscillator.PLL.PLLFRACN = 0U;
    if(HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                       RCC_CLOCKTYPE_PCLK3;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    clocks.APB3CLKDivider = RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_5) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    __HAL_RCC_CRS_CLK_ENABLE();
    crs.Prescaler = RCC_CRS_SYNC_DIV1;
    crs.Source = RCC_CRS_SYNC_SOURCE_USB;
    crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
    crs.ErrorLimitValue = 34U;
    crs.HSI48CalibrationValue = 32U;
    HAL_RCCEx_CRSConfig(&crs);
    __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);

    SystemCoreClockUpdate();
    bsp_clock_hw_capture_snapshot();
    return (g_bsp_clock_snapshot.sysclk_hz == 250000000U) ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Validate expected PLL divisors and physical SYSCLK result. */
static bool bsp_clock_hw_config_is_valid(const bsp_clock_config_t *config)
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

/** @brief Capture the currently active RCC clock frequencies. */
static void bsp_clock_hw_capture_snapshot(void)
{
    g_bsp_clock_snapshot.sysclk_hz = HAL_RCC_GetSysClockFreq();
    g_bsp_clock_snapshot.hclk_hz = HAL_RCC_GetHCLKFreq();
    g_bsp_clock_snapshot.pclk1_hz = HAL_RCC_GetPCLK1Freq();
    g_bsp_clock_snapshot.pclk2_hz = HAL_RCC_GetPCLK2Freq();
    g_bsp_clock_snapshot.pclk3_hz = HAL_RCC_GetPCLK3Freq();
}

/** @brief Implement bsp_clock_init() without changing the configured clock tree. */
bsp_status_t bsp_clock_init(const bsp_clock_config_t *config)
{
    if(!bsp_clock_hw_config_is_valid(config))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(g_bsp_clock_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    SystemCoreClockUpdate();
    bsp_clock_hw_capture_snapshot();
    if((__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_PLLCLK) ||
       (__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) == 0U) ||
       (g_bsp_clock_snapshot.sysclk_hz != config->expected_sysclk_hz))
    {
        return BSP_STATUS_IO_ERROR;
    }

    g_bsp_clock_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Implement bsp_clock_get_hz() for STM32H5 bus and timer clocks. */
bsp_status_t bsp_clock_get_hz(bsp_clock_id_t clock_id, uint32_t *frequency_hz)
{
    RCC_ClkInitTypeDef clocks = {0};
    uint32_t flash_latency;

    if(frequency_hz == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!g_bsp_clock_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    switch(clock_id)
    {
        case BSP_CLOCK_SYSCLK:
            *frequency_hz = g_bsp_clock_snapshot.sysclk_hz;
            break;
        case BSP_CLOCK_HCLK:
            *frequency_hz = g_bsp_clock_snapshot.hclk_hz;
            break;
        case BSP_CLOCK_PCLK1:
            *frequency_hz = g_bsp_clock_snapshot.pclk1_hz;
            break;
        case BSP_CLOCK_PCLK2:
            *frequency_hz = g_bsp_clock_snapshot.pclk2_hz;
            break;
        case BSP_CLOCK_PCLK3:
            *frequency_hz = g_bsp_clock_snapshot.pclk3_hz;
            break;
        case BSP_CLOCK_TIMER_APB1:
            HAL_RCC_GetClockConfig(&clocks, &flash_latency);
            *frequency_hz = clocks.APB1CLKDivider == RCC_HCLK_DIV1 ?
                            g_bsp_clock_snapshot.pclk1_hz :
                            g_bsp_clock_snapshot.pclk1_hz * 2U;
            break;
        case BSP_CLOCK_TIMER_APB2:
            HAL_RCC_GetClockConfig(&clocks, &flash_latency);
            *frequency_hz = clocks.APB2CLKDivider == RCC_HCLK_DIV1 ?
                            g_bsp_clock_snapshot.pclk2_hz :
                            g_bsp_clock_snapshot.pclk2_hz * 2U;
            break;
        default:
            return BSP_STATUS_INVALID_ARGUMENT;
    }

    return BSP_STATUS_OK;
}

/** @brief Implement bsp_clock_get_snapshot() through static BSP-owned data. */
const bsp_clock_snapshot_t *bsp_clock_get_snapshot(void)
{
    return &g_bsp_clock_snapshot;
}
