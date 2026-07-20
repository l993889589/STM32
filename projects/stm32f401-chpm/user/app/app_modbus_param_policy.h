/**
 * @file app_modbus_param_policy.h
 * @brief Pure Modbus-to-parameter transaction and fan-output policy.
 */

#ifndef APP_MODBUS_PARAM_POLICY_H
#define APP_MODBUS_PARAM_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "param.h"

/** @brief Writable Modbus configuration register snapshot. */
typedef struct
{
    uint16_t address;
    uint16_t fan_mode;
    uint16_t manual_pwm;
} app_modbus_param_registers_t;

/** @brief Fan hardware action required after a durable parameter commit. */
typedef enum
{
    APP_MODBUS_FAN_OUTPUT_NONE = 0,
    APP_MODBUS_FAN_OUTPUT_MANUAL,
    APP_MODBUS_FAN_OUTPUT_AUTO
} app_modbus_fan_output_t;

/** @brief Prepared all-or-nothing persistent update and post-commit action. */
typedef struct
{
    PARAM_T candidate;
    bool changed;
    app_modbus_fan_output_t fan_output;
    uint16_t fan_pwm;
} app_modbus_param_update_t;

/**
 * @brief Prepare one validated configuration update without changing state.
 * @param current Current durable/runtime parameter snapshot.
 * @param current_auto_pwm Current live automatic-mode PWM command.
 * @param before Register image before the Modbus server processed the request.
 * @param after Register image after the Modbus server processed the request.
 * @param update Receives the candidate and required post-commit fan action.
 * @return true for a valid request, otherwise false.
 */
bool app_modbus_param_prepare(
    const PARAM_T *current,
    uint16_t current_auto_pwm,
    const app_modbus_param_registers_t *before,
    const app_modbus_param_registers_t *after,
    app_modbus_param_update_t *update);

#endif /* APP_MODBUS_PARAM_POLICY_H */
