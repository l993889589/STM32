/**
 * @file bsp_pwm.h
 * @brief Logical PWM interface configured in physical units.
 */

#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Logical board PWM roles. */
typedef enum
{
    BOARD_PWM_LCD_BACKLIGHT = 0,
    BOARD_PWM_COUNT
} bsp_pwm_role_t;

/** @brief Requested PWM frequency and duty. */
typedef struct
{
    uint32_t frequency_hz;
    uint16_t duty_permille;
} bsp_pwm_config_t;

/** @brief Solved timer settings and achieved physical values. */
typedef struct
{
    uint32_t requested_frequency_hz;
    uint32_t achieved_frequency_hz;
    uint32_t error_ppm;
    uint32_t timer_clock_hz;
    uint32_t prescaler;
    uint32_t auto_reload;
    uint32_t compare;
    uint16_t requested_duty_permille;
} bsp_pwm_result_t;

/**
 * @brief Initialize a logical PWM from physical-unit configuration.
 * @param role Logical PWM role.
 * @param config Requested frequency in hertz and duty in permille.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_init(bsp_pwm_role_t role,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result);

/**
 * @brief Reconfigure an initialized logical PWM.
 * @param role Logical PWM role.
 * @param config Requested frequency in hertz and duty in permille.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_configure(bsp_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result);

/** @brief Start an initialized logical PWM. @param role Logical PWM role. @return BSP status. */
bsp_status_t bsp_pwm_start(bsp_pwm_role_t role);
/** @brief Stop an initialized logical PWM. @param role Logical PWM role. @return BSP status. */
bsp_status_t bsp_pwm_stop(bsp_pwm_role_t role);

/**
 * @brief Read the last achieved PWM configuration.
 * @param role Logical PWM role.
 * @param result Destination for achieved values.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_get_result(bsp_pwm_role_t role, bsp_pwm_result_t *result);

#endif /* BSP_PWM_H */
