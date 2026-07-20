/**
 * @file app_fan.c
 * @brief Product fan policy over the logical BSP PWM role.
 */

#include "app_fan.h"

#include <stddef.h>

#include "app_main.h"
#include "bsp_pwm.h"
#include "param.h"

#define APP_FAN_DEFAULT_FREQUENCY_HZ (25000UL)
#define APP_FAN_MIN_DUTY             (4000U)
#define APP_FAN_MAX_DUTY             (10000U)

fan_handle fan;

/** @brief Clamp the product fan command to its validated operating range. */
static uint16_t app_fan_clamp_duty(uint16_t duty)
{
    if(duty < APP_FAN_MIN_DUTY)
        return APP_FAN_MIN_DUTY;
    if(duty > APP_FAN_MAX_DUTY)
        return APP_FAN_MAX_DUTY;
    return duty;
}

/** @brief Apply the current physical frequency and duty request. */
static void app_fan_apply(void)
{
    bsp_pwm_config_t config;
    bsp_status_t status;

    config.frequency_hz = fan.fre;
    config.duty_permyriad = fan.duty;
    status = bsp_pwm_configure(BSP_PWM_FAN, &config, NULL);
    if(status == BSP_STATUS_NOT_READY)
        (void)bsp_pwm_init(BSP_PWM_FAN, &config, NULL);
}

/** @brief Preserve the legacy discrete-level entry point. */
void app_fan_set(uint8_t level)
{
    static const uint16_t duty_by_level[] =
        {4000U, 4000U, 5000U, 5000U, 5000U, 5000U, 7500U, 7500U, 10000U};

    if(level < (sizeof(duty_by_level) / sizeof(duty_by_level[0])))
    {
        fan.duty = duty_by_level[level];
        app_fan_apply();
    }
}

/** @brief Initialize the fan from the persisted automatic/manual selection. */
void app_fan_init(void)
{
    fan.state = 0U;
    fan.duty = param_fan_mode_get() ?
               param_pwm_manual_get() : param_pwm_auto_get();
    fan.duty = app_fan_clamp_duty(fan.duty);
    fan.fre = APP_FAN_DEFAULT_FREQUENCY_HZ;
    app_fan_apply();
}

/** @brief Apply the manual duty already committed by the parameter owner. */
void app_fan_set_duty(uint16_t duty)
{
    duty = app_fan_clamp_duty(duty);
    if(param_fan_mode_get() != 1U)
        return;
    acquire_mutex();
    g_tVar.pwm_manual = duty;
    release_mutex();
    fan.duty = duty;
    app_fan_apply();
}

/** @brief Apply an automatic-mode duty request. */
void app_fan_set_duty_by_auto(uint16_t duty)
{
    duty = app_fan_clamp_duty(duty);
    if(param_fan_mode_get() != 0U)
        return;
    acquire_mutex();
    g_tVar.pwm_auto = duty;
    release_mutex();
    fan.duty = duty;
    app_fan_apply();
}

/** @brief Stage a new carrier frequency for the application process loop. */
void app_fan_set_fre(uint32_t frequency_hz)
{
    if(frequency_hz == 0U)
        return;
    fan.fre = frequency_hz;
    fan.state = 1U;
}

/** @brief Stage a complete physical output request. */
void app_fan_setall(uint32_t frequency_hz, uint16_t duty)
{
    if(frequency_hz == 0U)
        return;
    fan.fre = frequency_hz;
    fan.duty = app_fan_clamp_duty(duty);
    fan.state = 1U;
}

/** @brief Commit a staged fan request once. */
void app_fan_process(void)
{
    if(fan.state != 0U)
    {
        app_fan_apply();
        fan.state = 0U;
    }
}
