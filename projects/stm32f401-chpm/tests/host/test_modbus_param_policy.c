/**
 * @file test_modbus_param_policy.c
 * @brief Host tests for CHPM Modbus persistent-register semantics.
 */

#include "app_modbus_param_policy.h"

#include <assert.h>
#include <stdio.h>

/** @brief Exercise no-op, manual preset, mode switch, and invalid requests. */
int main(void)
{
    PARAM_T current = {10U, 0U, 7U, 6500U, 5200U};
    app_modbus_param_registers_t before = {10U, 0U, 6500U};
    app_modbus_param_registers_t after = before;
    app_modbus_param_update_t update;

    assert(app_modbus_param_prepare(&current, 5200U,
                                    &before, &after, &update));
    assert(!update.changed);
    assert(update.fan_output == APP_MODBUS_FAN_OUTPUT_NONE);

    /* Automatic mode accepts and persists a manual preset without PWM change. */
    after.manual_pwm = 7000U;
    assert(app_modbus_param_prepare(&current, 5200U,
                                    &before, &after, &update));
    assert(update.changed && update.candidate.pwm_manual == 7000U);
    assert(update.candidate.mode == 0U);
    assert(update.fan_output == APP_MODBUS_FAN_OUTPUT_NONE);

    /* Switching to manual applies the final manual preset after persistence. */
    after.fan_mode = 1U;
    assert(app_modbus_param_prepare(&current, 5200U,
                                    &before, &after, &update));
    assert(update.fan_output == APP_MODBUS_FAN_OUTPUT_MANUAL);
    assert(update.fan_pwm == 7000U);

    /* Switching back to auto restores the live automatic command. */
    current.mode = 1U;
    before.fan_mode = 1U;
    before.manual_pwm = 7000U;
    after = before;
    after.fan_mode = 0U;
    assert(app_modbus_param_prepare(&current, 5300U,
                                    &before, &after, &update));
    assert(update.fan_output == APP_MODBUS_FAN_OUTPUT_AUTO);
    assert(update.fan_pwm == 5300U);

    /* Address-only changes remain output-neutral. */
    current.mode = 0U;
    before.fan_mode = 0U;
    after = before;
    after.address = 42U;
    assert(app_modbus_param_prepare(&current, 5300U,
                                    &before, &after, &update));
    assert(update.candidate.Addr485 == 42U);
    assert(update.fan_output == APP_MODBUS_FAN_OUTPUT_NONE);

    after.address = 0U;
    assert(!app_modbus_param_prepare(&current, 5300U,
                                     &before, &after, &update));

    puts("CHPM Modbus parameter policy tests passed");
    return 0;
}
