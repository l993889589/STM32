#ifndef BSP_TIM_H
#define BSP_TIM_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BSP_TIM_MIN_DELAY_US 5U

typedef enum
{
    BSP_TIM_CHANNEL_1 = 1,
    BSP_TIM_CHANNEL_2,
    BSP_TIM_CHANNEL_3,
    BSP_TIM_CHANNEL_4
} bsp_tim_channel_t;

typedef void (*bsp_tim_callback_t)(void *argument);

HAL_StatusTypeDef bsp_tim_init(void);

/* One-shot callback runs in TIM2 interrupt context and must not block. */
HAL_StatusTypeDef bsp_tim_start(bsp_tim_channel_t channel,
                                uint32_t delay_us,
                                bsp_tim_callback_t callback,
                                void *argument);
HAL_StatusTypeDef bsp_tim_stop(bsp_tim_channel_t channel);
uint32_t bsp_tim_now_us(void);

#endif
