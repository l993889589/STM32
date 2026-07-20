/**
 * @file app_modbus_param_policy.c
 * @brief Pure Modbus configuration transaction and fan-output decisions.
 */

#include "app_modbus_param_policy.h"

#include <stddef.h>

#define APP_MODBUS_ADDRESS_MIN (1U)
#define APP_MODBUS_ADDRESS_MAX (247U)
#define APP_MODBUS_PWM_MIN     (4000U)
#define APP_MODBUS_PWM_MAX     (10000U)

/** @brief Return true when a writable register snapshot is in range. */
static bool app_modbus_registers_valid(
    const app_modbus_param_registers_t *registers)
{
    return registers != NULL &&
           registers->address >= APP_MODBUS_ADDRESS_MIN &&
           registers->address <= APP_MODBUS_ADDRESS_MAX &&
           registers->fan_mode <= 1U &&
           registers->manual_pwm >= APP_MODBUS_PWM_MIN &&
           registers->manual_pwm <= APP_MODBUS_PWM_MAX;
}

/** @brief Prepare a persistent candidate and its post-commit fan action. */
bool app_modbus_param_prepare(
    const PARAM_T *current,
    uint16_t current_auto_pwm,
    const app_modbus_param_registers_t *before,
    const app_modbus_param_registers_t *after,
    app_modbus_param_update_t *update)
{
    bool address_changed;
    bool mode_changed;
    bool manual_changed;

    if(current == NULL || update == NULL ||
       !app_modbus_registers_valid(before) ||
       !app_modbus_registers_valid(after) ||
       current_auto_pwm < APP_MODBUS_PWM_MIN ||
       current_auto_pwm > APP_MODBUS_PWM_MAX)
        return false;

    address_changed = after->address != before->address;
    mode_changed = after->fan_mode != before->fan_mode;
    manual_changed = after->manual_pwm != before->manual_pwm;
    update->candidate = *current;
    update->changed = address_changed || mode_changed || manual_changed;
    update->fan_output = APP_MODBUS_FAN_OUTPUT_NONE;
    update->fan_pwm = 0U;

    if(address_changed)
        update->candidate.Addr485 = (uint8_t)after->address;
    if(mode_changed)
        update->candidate.mode = (uint8_t)after->fan_mode;
    if(manual_changed)
        update->candidate.pwm_manual = after->manual_pwm;

    /*
     * Register 0x0002 always stores the manual preset.  While automatic mode
     * remains selected it must not disturb the live automatic fan command.
     */
    if(update->candidate.mode != 0U && (mode_changed || manual_changed))
    {
        update->fan_output = APP_MODBUS_FAN_OUTPUT_MANUAL;
        update->fan_pwm = update->candidate.pwm_manual;
    }
    else if(update->candidate.mode == 0U && mode_changed)
    {
        update->fan_output = APP_MODBUS_FAN_OUTPUT_AUTO;
        update->fan_pwm = current_auto_pwm;
    }
    return true;
}
