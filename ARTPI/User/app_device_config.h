#ifndef APP_DEVICE_CONFIG_H
#define APP_DEVICE_CONFIG_H

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define APP_DEVICE_CONFIG_DEFAULT_MODBUS_UNIT_ID 1U

typedef struct
{
    uint8_t modbus_unit_id;
} app_device_config_t;

typedef struct
{
    uint32_t slot_a_sequence;
    uint32_t slot_b_sequence;
    uint8_t slot_a_valid;
    uint8_t slot_b_valid;
    uint8_t slot_a_modbus_unit_id;
    uint8_t slot_b_modbus_unit_id;
} app_device_config_diagnostics_t;

HAL_StatusTypeDef app_device_config_load(app_device_config_t *config,
                                         uint8_t *loaded_from_flash);
HAL_StatusTypeDef app_device_config_save(const app_device_config_t *config);
HAL_StatusTypeDef app_device_config_get_diagnostics(
    app_device_config_diagnostics_t *diagnostics);

#endif
