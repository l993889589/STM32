#ifndef APP_MODBUS_RTU_H
#define APP_MODBUS_RTU_H

#include "stm32h7xx_hal.h"

#define APP_MODBUS_RTU_UNIT_ID   1U
#define APP_MODBUS_RTU_BAUD_RATE 115200U

typedef struct
{
    uint8_t unit_id;
    uint8_t red_led_on;
    uint8_t buzzer_on;
} app_modbus_rtu_config_t;

typedef struct
{
    uint32_t received_bytes;
    uint32_t received_frames;
    uint32_t replied_frames;
    uint32_t crc_errors;
    uint32_t malformed_frames;
    uint32_t ldc_overflow;
    uint32_t ldc_drop;
} app_modbus_rtu_diagnostics_t;

HAL_StatusTypeDef app_modbus_rtu_start(void);
HAL_StatusTypeDef app_modbus_rtu_request_config(
    const app_modbus_rtu_config_t *config);
HAL_StatusTypeDef app_modbus_rtu_get_config(app_modbus_rtu_config_t *config);
HAL_StatusTypeDef app_modbus_rtu_get_diagnostics(
    app_modbus_rtu_diagnostics_t *diagnostics);

#endif
