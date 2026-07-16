/**
 * @file bsp_timer.h
 * @brief Public TIM4 board timing handle and initialization interface.
 */

#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include "bsp_status.h"
#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim4;

/** @brief Initialize the TIM4 board timing peripheral. */
bsp_status_t bsp_timer_init(void);

#endif
