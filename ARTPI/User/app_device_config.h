#ifndef APP_DEVICE_CONFIG_H
#define APP_DEVICE_CONFIG_H

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define APP_DEVICE_CONFIG_DEFAULT_MODBUS_UNIT_ID      1U
#define APP_DEVICE_CONFIG_MAX_MASTER_DEVICES         10U
#define APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT        4U
#define APP_DEVICE_CONFIG_MAX_RANGE_QUANTITY         16U
#define APP_DEVICE_CONFIG_DEFAULT_POLL_PERIOD_MS    1000U
#define APP_DEVICE_CONFIG_MIN_POLL_PERIOD_MS         100U
#define APP_DEVICE_CONFIG_MAX_POLL_PERIOD_MS       60000U
#define APP_DEVICE_CONFIG_DEFAULT_RESPONSE_TIMEOUT_MS 200U
#define APP_DEVICE_CONFIG_MIN_RESPONSE_TIMEOUT_MS     20U
#define APP_DEVICE_CONFIG_MAX_RESPONSE_TIMEOUT_MS   5000U

typedef enum
{
    APP_RS485_ROLE_SLAVE = 0,
    APP_RS485_ROLE_MASTER = 1
} app_rs485_role_t;

typedef enum
{
    APP_MODBUS_REGISTER_COILS = 0,
    APP_MODBUS_REGISTER_DISCRETE_INPUTS,
    APP_MODBUS_REGISTER_HOLDING_REGISTERS,
    APP_MODBUS_REGISTER_INPUT_REGISTERS
} app_modbus_register_class_t;

typedef struct
{
    uint16_t address;
    uint16_t quantity;
} app_modbus_poll_range_t;

typedef struct
{
    uint8_t unit_id;
    uint8_t reserved;
    uint16_t response_timeout_ms;
    app_modbus_poll_range_t ranges[APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT];
} app_modbus_master_device_config_t;

typedef struct
{
    uint8_t rs485_role;
    uint8_t modbus_unit_id;
    uint8_t master_device_count;
    uint8_t reserved;
    uint16_t offline_probe_period_s;
    uint16_t poll_period_ms;
    app_modbus_master_device_config_t
        master_devices[APP_DEVICE_CONFIG_MAX_MASTER_DEVICES];
} app_device_config_t;

typedef struct
{
    uint32_t slot_a_sequence;
    uint32_t slot_b_sequence;
    uint16_t slot_a_version;
    uint16_t slot_b_version;
    uint8_t slot_a_valid;
    uint8_t slot_b_valid;
    uint8_t slot_a_modbus_unit_id;
    uint8_t slot_b_modbus_unit_id;
    uint8_t slot_a_rs485_role;
    uint8_t slot_b_rs485_role;
    uint8_t slot_a_master_device_count;
    uint8_t slot_b_master_device_count;
} app_device_config_diagnostics_t;

void app_device_config_set_defaults(app_device_config_t *config);
uint8_t app_device_config_validate(const app_device_config_t *config);
HAL_StatusTypeDef app_device_config_load(app_device_config_t *config,
                                         uint8_t *loaded_from_flash);
HAL_StatusTypeDef app_device_config_save(const app_device_config_t *config);
HAL_StatusTypeDef app_device_config_get_diagnostics(
    app_device_config_diagnostics_t *diagnostics);

#endif
