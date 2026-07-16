/**
 * @file bsp_pwm.h
 * @brief Public fan PWM control and legacy compatibility interface.
 */

#ifndef BSP_PWM_H
#define BSP_PWM_H

#include "bsp_status.h"
#include "stm32f4xx_hal.h"

/** @brief Set fan PWM frequency and duty in permyriad units. */
bsp_status_t bsp_fan_pwm_set(uint32_t frequency_hz, uint16_t duty_permyriad);

/** @brief Compatibility wrapper for validated legacy application call sites. */
void bsp_SetTIMOutPWM(GPIO_TypeDef *gpio, uint16_t pin, TIM_TypeDef *timer,
                      uint8_t channel, uint32_t frequency_hz,
                      uint32_t duty_permyriad);

#endif
