/**
 * @file bsp_pwm.h
 * @brief Logical fan PWM interface configured in physical units.
 */

#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Logical board PWM roles. */
typedef enum
{
    BSP_PWM_FAN = 0,
    BSP_PWM_COUNT
} bsp_pwm_role_t;

/** @brief Requested carrier frequency and duty in 1/10000 units. */
typedef struct
{
    uint32_t frequency_hz;
    uint16_t duty_permyriad;
} bsp_pwm_config_t;

/** @brief Timer solution and achieved physical output. */
typedef struct
{
    uint32_t requested_frequency_hz;
    uint32_t achieved_frequency_hz;
    uint32_t error_ppm;
    uint32_t timer_clock_hz;
    uint32_t prescaler;
    uint32_t auto_reload;
    uint32_t compare;
    uint16_t requested_duty_permyriad;
} bsp_pwm_result_t;

/** @brief Initialize PA8/TIM1_CH1 and start the requested fan waveform. */
bsp_status_t bsp_pwm_init(bsp_pwm_role_t role,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result);

/** @brief Reconfigure an initialized fan waveform. */
bsp_status_t bsp_pwm_configure(bsp_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result);

/** @brief Stop an initialized fan PWM output. */
bsp_status_t bsp_pwm_stop(bsp_pwm_role_t role);

/** @brief Copy the most recent achieved timer solution. */
bsp_status_t bsp_pwm_get_result(bsp_pwm_role_t role,
                                bsp_pwm_result_t *result);

#endif /* BSP_PWM_H */
