/**
 * @file bsp_timer.c
 * @brief TIM4 initialization used by board timing services.
 */

#include "bsp_timer.h"

#include <stdbool.h>

#include "board_config.h"

TIM_HandleTypeDef htim4;

static bool timer_initialized;

/** @brief Initialize TIM4 as the board timing peripheral. */
bsp_status_t bsp_timer_init(void)
{
    TIM_ClockConfigTypeDef clock_source = {0};
    TIM_MasterConfigTypeDef master = {0};

    if(timer_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    htim4.Instance = BOARD_DELAY_TIMER;
    htim4.Init.Prescaler = BOARD_DELAY_TIMER_PRESCALER;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 65535U;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    clock_source.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    master.MasterOutputTrigger = TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if(HAL_TIM_Base_Init(&htim4) != HAL_OK ||
       HAL_TIM_ConfigClockSource(&htim4, &clock_source) != HAL_OK ||
       HAL_TIMEx_MasterConfigSynchronization(&htim4, &master) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    timer_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Enable the TIM4 peripheral clock for HAL initialization. */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *timer)
{
    if(timer != NULL && timer->Instance == BOARD_DELAY_TIMER)
        __HAL_RCC_TIM4_CLK_ENABLE();
}

/** @brief Disable the TIM4 peripheral clock for HAL deinitialization. */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *timer)
{
    if(timer != NULL && timer->Instance == BOARD_DELAY_TIMER)
        __HAL_RCC_TIM4_CLK_DISABLE();
}
