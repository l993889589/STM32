#ifndef BSP_TIM_PWM_H
#define BSP_TIM_PWM_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BSP_TIM_PWM_PIN_DESCRIPTION "P1 pin 32, PH10, TIM5_CH1"

HAL_StatusTypeDef bsp_tim_pwm_init(void);

/* duty_permyriad: 0 is 0%, 5000 is 50%, and 10000 is 100%. */
HAL_StatusTypeDef bsp_tim_pwm_set(uint32_t frequency_hz,
                                  uint16_t duty_permyriad);
HAL_StatusTypeDef bsp_tim_pwm_stop(void);

#endif
