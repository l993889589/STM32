/**
 * @file bsp_pwm.h
 * @brief Physical-unit PWM public interface.
 */

#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdint.h>
#include "bsp_status.h"

typedef enum
{
    BOARD_PWM_LCD_BACKLIGHT = 0,
    BOARD_PWM_COUNT
} board_pwm_role_t;

typedef struct
{
    uint32_t frequency_hz;
    uint16_t duty_permille;
} bsp_pwm_config_t;

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
 * Initialize a logical PWM from physical-unit configuration.
 * @param role Logical PWM role.
 * @param config Requested frequency in hertz and duty in permille.
 * @param result Optional achieved-frequency and timer-setting result.
 * @return BSP status, including conflict or unsupported-range errors.
 */
bsp_status_t bsp_pwm_init(board_pwm_role_t role,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result);
/**
 * Reconfigure an initialized logical PWM without exposing PSC/ARR calculations.
 * @param role Logical PWM role.
 * @param config Requested frequency in hertz and duty in permille.
 * @param result Optional achieved configuration.
 * @return BSP status; shared-timer conflicts are reported.
 */
bsp_status_t bsp_pwm_configure(board_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result);
/**
 * Start output for an initialized logical PWM.
 * @param role Logical PWM role.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_start(board_pwm_role_t role);
/**
 * Stop a logical PWM and return its pin to the configured inactive state.
 * @param role Logical PWM role.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_stop(board_pwm_role_t role);
/**
 * Read the achieved PWM timer configuration.
 * @param role Logical PWM role.
 * @param result Receives achieved frequency, prescaler, period, and compare value.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_get_result(board_pwm_role_t role, bsp_pwm_result_t *result);

#endif
