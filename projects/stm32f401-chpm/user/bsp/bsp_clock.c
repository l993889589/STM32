/**
 * @file bsp_clock.c
 * @brief Fixed STM32F401 clock tree and timer-clock derivation.
 */

#include "bsp_clock.h"

#include <stdbool.h>

#include "stm32f4xx_hal.h"

#define BSP_HSE_FREQUENCY_HZ   25000000UL
#define BSP_SYSCLK_FREQUENCY_HZ 84000000UL
#define BSP_PCLK1_FREQUENCY_HZ  42000000UL
#define BSP_PCLK2_FREQUENCY_HZ  84000000UL

static bsp_clock_snapshot_t clock_snapshot;
static bool clock_initialized;

/** @brief Configure HSE, PLL, AHB, APB1, APB2, flash, and voltage scale. */
bsp_status_t bsp_clock_configure_system(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 25U;
    oscillator.PLL.PLLN = 336U;
    oscillator.PLL.PLLP = RCC_PLLP_DIV4;
    oscillator.PLL.PLLQ = 7U;
    if(HAL_RCC_OscConfig(&oscillator) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV2;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_2) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    SystemCoreClockUpdate();
    return BSP_STATUS_OK;
}

/** @brief Validate the configured clocks against the board contract. */
bsp_status_t bsp_clock_init(void)
{
    if(clock_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    clock_snapshot.sysclk_hz = HAL_RCC_GetSysClockFreq();
    clock_snapshot.hclk_hz = HAL_RCC_GetHCLKFreq();
    clock_snapshot.pclk1_hz = HAL_RCC_GetPCLK1Freq();
    clock_snapshot.pclk2_hz = HAL_RCC_GetPCLK2Freq();
    if(clock_snapshot.sysclk_hz != BSP_SYSCLK_FREQUENCY_HZ ||
       clock_snapshot.hclk_hz != BSP_SYSCLK_FREQUENCY_HZ ||
       clock_snapshot.pclk1_hz != BSP_PCLK1_FREQUENCY_HZ ||
       clock_snapshot.pclk2_hz != BSP_PCLK2_FREQUENCY_HZ ||
       HSE_VALUE != BSP_HSE_FREQUENCY_HZ)
        return BSP_STATUS_CONFLICT;

    clock_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Return a core, bus, or timer clock in hertz. */
bsp_status_t bsp_clock_get_hz(bsp_clock_id_t clock_id, uint32_t *frequency_hz)
{
    if(frequency_hz == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!clock_initialized)
        return BSP_STATUS_NOT_READY;

    switch(clock_id)
    {
        case BSP_CLOCK_SYSCLK:
            *frequency_hz = clock_snapshot.sysclk_hz;
            break;
        case BSP_CLOCK_HCLK:
            *frequency_hz = clock_snapshot.hclk_hz;
            break;
        case BSP_CLOCK_PCLK1:
            *frequency_hz = clock_snapshot.pclk1_hz;
            break;
        case BSP_CLOCK_PCLK2:
            *frequency_hz = clock_snapshot.pclk2_hz;
            break;
        case BSP_CLOCK_TIMER_APB1:
            *frequency_hz = ((RCC->CFGR & RCC_CFGR_PPRE1) ==
                             RCC_CFGR_PPRE1_DIV1) ?
                            clock_snapshot.pclk1_hz :
                            clock_snapshot.pclk1_hz * 2U;
            break;
        case BSP_CLOCK_TIMER_APB2:
            *frequency_hz = ((RCC->CFGR & RCC_CFGR_PPRE2) ==
                             RCC_CFGR_PPRE2_DIV1) ?
                            clock_snapshot.pclk2_hz :
                            clock_snapshot.pclk2_hz * 2U;
            break;
        default:
            return BSP_STATUS_INVALID_ARGUMENT;
    }
    return BSP_STATUS_OK;
}

/** @brief Return the static clock snapshot captured during BSP init. */
const bsp_clock_snapshot_t *bsp_clock_get_snapshot(void)
{
    return clock_initialized ? &clock_snapshot : NULL;
}
