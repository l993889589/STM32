#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
} bsp_pwm_t;

bool bsp_pwm_start(const bsp_pwm_t *pwm);
bool bsp_pwm_stop(const bsp_pwm_t *pwm);
bool bsp_pwm_set_duty_permille(const bsp_pwm_t *pwm, uint16_t duty_permille);
bool bsp_pwm_set_duty_percent(const bsp_pwm_t *pwm, uint8_t duty_percent);
bool bsp_pwm_set_frequency(const bsp_pwm_t *pwm, uint32_t timer_clock_hz, uint32_t frequency_hz);
uint32_t bsp_pwm_get_period(const bsp_pwm_t *pwm);
uint32_t bsp_pwm_get_pulse(const bsp_pwm_t *pwm);

#endif /* BSP_PWM_H */
