/**
 * @file stm32h5xx_hal_timebase_tim.c
 * @brief TIM17 HAL timebase that leaves SysTick exclusively to ThreadX.
 */

#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_tim.h"

TIM_HandleTypeDef htim17;

/**
 * @brief Configure TIM17 as the 1 kHz HAL timebase.
 * @param tick_priority NVIC priority requested by HAL.
 * @return HAL status.
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    RCC_ClkInitTypeDef clock_config;
    uint32_t timer_clock;
    uint32_t flash_latency;
    HAL_StatusTypeDef status;

    __HAL_RCC_TIM17_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clock_config, &flash_latency);
    timer_clock = HAL_RCC_GetPCLK2Freq();

    htim17.Instance = TIM17;
    htim17.Init.Period = 99U;
    htim17.Init.Prescaler = (timer_clock / 100000U) - 1U;
    htim17.Init.ClockDivision = 0U;
    htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
    status = HAL_TIM_Base_Init(&htim17);
    if(status == HAL_OK)
    {
        status = HAL_TIM_Base_Start_IT(&htim17);
    }
    if((status == HAL_OK) && (tick_priority < (1UL << __NVIC_PRIO_BITS)))
    {
        HAL_NVIC_SetPriority(TIM17_IRQn, tick_priority, 0U);
        HAL_NVIC_EnableIRQ(TIM17_IRQn);
        uwTickPrio = tick_priority;
    }
    else if(status == HAL_OK)
    {
        status = HAL_ERROR;
    }
    return status;
}

/** @brief Suspend the HAL TIM17 tick interrupt. */
void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&htim17, TIM_IT_UPDATE);
}

/** @brief Resume the HAL TIM17 tick interrupt. */
void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&htim17, TIM_IT_UPDATE);
}

/** @brief Advance the HAL millisecond counter from TIM17. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *handle)
{
    if((handle != NULL) && (handle->Instance == TIM17))
    {
        HAL_IncTick();
    }
}
