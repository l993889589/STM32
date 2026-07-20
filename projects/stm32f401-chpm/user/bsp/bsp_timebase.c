/**
 * @file bsp_timebase.c
 * @brief TIM11 ownership for the STM32 HAL millisecond time base.
 */

#include "bsp_timebase.h"

#include "stm32f4xx_hal.h"

static TIM_HandleTypeDef g_hal_tick_timer;

/** @brief Configure TIM11 for the HAL's 1 kHz time base. */
HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    uint32_t timer_clock_hz = HAL_RCC_GetPCLK2Freq();
    HAL_StatusTypeDef status;

    if((RCC->CFGR & RCC_CFGR_PPRE2) != RCC_HCLK_DIV1)
        timer_clock_hz *= 2U;
    if(timer_clock_hz < 1000000U)
        return HAL_ERROR;

    __HAL_RCC_TIM11_CLK_ENABLE();
    g_hal_tick_timer.Instance = TIM11;
    g_hal_tick_timer.Init.Prescaler = timer_clock_hz / 1000000U - 1U;
    g_hal_tick_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_hal_tick_timer.Init.Period = 1000U - 1U;
    g_hal_tick_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_hal_tick_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    status = HAL_TIM_Base_Init(&g_hal_tick_timer);
    if(status != HAL_OK)
        return status;
    status = HAL_TIM_Base_Start_IT(&g_hal_tick_timer);
    if(status != HAL_OK)
        return status;
    if(tick_priority >= (1UL << __NVIC_PRIO_BITS))
        return HAL_ERROR;

    HAL_NVIC_SetPriority(TIM1_TRG_COM_TIM11_IRQn, tick_priority, 0U);
    HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);
    uwTickPrio = tick_priority;
    return HAL_OK;
}

/** @brief Pause only the HAL time-base update interrupt. */
void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&g_hal_tick_timer, TIM_IT_UPDATE);
}

/** @brief Resume the HAL time-base update interrupt. */
void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&g_hal_tick_timer, TIM_IT_UPDATE);
}

/** @brief Provide an application-safe wrapper around HAL tick suspension. */
void bsp_timebase_suspend(void)
{
    HAL_SuspendTick();
}

/** @brief Provide an application-safe wrapper around HAL tick resumption. */
void bsp_timebase_resume(void)
{
    HAL_ResumeTick();
}

/** @brief Increment HAL time from the private TIM11 instance. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer)
{
    if(timer != &g_hal_tick_timer)
        return;
    HAL_IncTick();
}

/** @brief Own the shared TIM11 vector next to the HAL time-base handle. */
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_hal_tick_timer);
}
