/**
 * @file mcu_hal_timebase.c
 * @brief TIM17 HAL millisecond timebase used while ThreadX owns SysTick.
 */

#include "mcu_hal_timebase.h"

#include "stm32h5xx_hal.h"

static TIM_HandleTypeDef g_hal_tick_timer;

/** @brief Configure TIM17 for the HAL 1 ms timebase. */
HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    RCC_ClkInitTypeDef clocks = {0};
    uint32_t flash_latency;
    uint32_t timer_clock_hz;
    uint32_t prescaler;

    if(tick_priority >= (1UL << __NVIC_PRIO_BITS))
    {
        return HAL_ERROR;
    }

    __HAL_RCC_TIM17_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clocks, &flash_latency);
    timer_clock_hz = HAL_RCC_GetPCLK2Freq();
    if(clocks.APB2CLKDivider != RCC_HCLK_DIV1)
    {
        timer_clock_hz *= 2U;
    }
    if(timer_clock_hz < 1000000U)
    {
        return HAL_ERROR;
    }

    prescaler = (timer_clock_hz / 1000000U) - 1U;
    g_hal_tick_timer.Instance = TIM17;
    g_hal_tick_timer.Init.Prescaler = prescaler;
    g_hal_tick_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_hal_tick_timer.Init.Period = 1000U - 1U;
    g_hal_tick_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_hal_tick_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if((HAL_TIM_Base_Init(&g_hal_tick_timer) != HAL_OK) ||
       (HAL_TIM_Base_Start_IT(&g_hal_tick_timer) != HAL_OK))
    {
        return HAL_ERROR;
    }

    HAL_NVIC_SetPriority(TIM17_IRQn, tick_priority, 0U);
    HAL_NVIC_EnableIRQ(TIM17_IRQn);
    uwTickPrio = tick_priority;
    return HAL_OK;
}

/** @brief Suspend TIM17 HAL tick interrupts. */
void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&g_hal_tick_timer, TIM_IT_UPDATE);
}

/** @brief Resume TIM17 HAL tick interrupts. */
void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&g_hal_tick_timer, TIM_IT_UPDATE);
}

/** @brief Dispatch TIM17 to the private HAL timer handle. */
void mcu_hal_timebase_irq_from_isr(void)
{
    HAL_TIM_IRQHandler(&g_hal_tick_timer);
}
