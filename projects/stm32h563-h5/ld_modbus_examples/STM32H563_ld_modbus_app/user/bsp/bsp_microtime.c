/**
 * @file bsp_microtime.c
 * @brief TIM2 or DWT implementation of the BSP microsecond timestamp API.
 *
 * The TIM2 backend uses a 32-bit, 1 MHz free-running counter without update
 * interrupts. It owns TIM2 exclusively. The current board clock config keeps
 * APB1 at HCLK/1, so the TIM2 input clock equals HAL_RCC_GetPCLK1Freq().
 */

#include "bsp_microtime.h"

#include "bsp_microtime_config.h"

#if BSP_MICROTIME_BACKEND == BSP_MICROTIME_BACKEND_DWT

#include "bsp_dwt.h"

/** @brief Initialize the DWT-backed microsecond counter. */
bsp_status_t bsp_microtime_init(void)
{
    return bsp_dwt_init() ? BSP_STATUS_OK : BSP_STATUS_NOT_SUPPORTED;
}

/** @brief Convert the current DWT cycle count to wrapping microseconds. */
uint32_t bsp_microtime_now_us(void)
{
    return bsp_dwt_get_us();
}

#elif BSP_MICROTIME_BACKEND == BSP_MICROTIME_BACKEND_TIM2

#include "stm32h5xx_hal.h"

#define BSP_MICROTIME_HZ (1000000U)

static uint8_t g_microtime_initialized;

/** @brief Configure TIM2 as an interrupt-free 1 MHz, 32-bit counter. */
bsp_status_t bsp_microtime_init(void)
{
    uint32_t timer_clock_hz;
    uint32_t prescaler;

    if(g_microtime_initialized != 0U)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    /* The example clock tree configures APB1 without division. */
    timer_clock_hz = HAL_RCC_GetPCLK1Freq();
    if((timer_clock_hz < BSP_MICROTIME_HZ) ||
       ((timer_clock_hz % BSP_MICROTIME_HZ) != 0U))
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    prescaler = (timer_clock_hz / BSP_MICROTIME_HZ) - 1U;
    if(prescaler > 0xFFFFU)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2->CR1 = 0U;
    TIM2->CR2 = 0U;
    TIM2->SMCR = 0U;
    TIM2->DIER = 0U;
    TIM2->PSC = prescaler;
    TIM2->ARR = 0xFFFFFFFFUL;
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    TIM2->CR1 = TIM_CR1_CEN;

    if((TIM2->CR1 & TIM_CR1_CEN) == 0U)
    {
        return BSP_STATUS_IO_ERROR;
    }

    g_microtime_initialized = 1U;
    return BSP_STATUS_OK;
}

/** @brief Read the current TIM2 count as a wrapping microsecond timestamp. */
uint32_t bsp_microtime_now_us(void)
{
    return (g_microtime_initialized != 0U) ? TIM2->CNT : 0U;
}

#endif
