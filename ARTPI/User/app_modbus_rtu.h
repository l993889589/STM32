#ifndef APP_MODBUS_RTU_H
#define APP_MODBUS_RTU_H

#include "stm32h7xx_hal.h"

#include "app_device_config.h"
#include "app_h563_ota_master.h"

#define APP_MODBUS_RTU_UNIT_ID          1U
#define APP_MODBUS_RTU_BAUD_RATE   115200U
#define APP_MODBUS_COMMAND_QUEUE_SIZE  16U

typedef enum
{
    APP_MODBUS_DEVICE_ONLINE = 0,
    APP_MODBUS_DEVICE_SUSPECT,
    APP_MODBUS_DEVICE_OFFLINE,
    APP_MODBUS_DEVICE_PROBING
} app_modbus_device_state_t;

typedef enum
{
    APP_MODBUS_COMMAND_WRITE_COIL = 0,
    APP_MODBUS_COMMAND_WRITE_REGISTER
} app_modbus_command_type_t;

typedef enum
{
    APP_MODBUS_COMMAND_RESULT_NONE = 0,
    APP_MODBUS_COMMAND_RESULT_QUEUED,
    APP_MODBUS_COMMAND_RESULT_OK,
    APP_MODBUS_COMMAND_RESULT_TIMEOUT,
    APP_MODBUS_COMMAND_RESULT_EXCEPTION,
    APP_MODBUS_COMMAND_RESULT_PROTOCOL_ERROR,
    APP_MODBUS_COMMAND_RESULT_CANCELLED
} app_modbus_command_result_t;

typedef struct
{
    app_device_config_t persistent;
    uint8_t red_led_on;
    uint8_t buzzer_on;
} app_modbus_rtu_config_t;

typedef struct
{
    uint8_t device_index;
    uint8_t type;
    uint16_t address;
    uint16_t value;
} app_modbus_command_t;

typedef struct
{
    uint8_t unit_id;
    uint8_t state;
    uint8_t consecutive_failures;
    uint8_t backoff_step;
    uint8_t last_function;
    uint8_t last_exception;
    uint16_t reserved;
    uint32_t successful_polls;
    uint32_t timeouts;
    uint32_t protocol_errors;
    uint32_t last_success_ms;
    uint32_t next_action_ms;
    uint16_t values[APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT]
                   [APP_DEVICE_CONFIG_MAX_RANGE_QUANTITY];
} app_modbus_master_device_status_t;

typedef struct
{
    uint8_t role;
    uint8_t active_transaction;
    uint8_t active_unit_id;
    uint8_t active_function;
    uint8_t command_queue_depth;
    uint8_t device_count;
    uint16_t reserved;
    uint32_t received_bytes;
    uint32_t received_frames;
    uint32_t replied_frames;
    uint32_t crc_errors;
    uint32_t malformed_frames;
    uint32_t ldc_overflow;
    uint32_t ldc_drop;
    uint32_t polls_completed;
    uint32_t poll_timeouts;
    uint32_t commands_queued;
    uint32_t commands_completed;
    uint32_t commands_failed;
    uint32_t priority_dispatches;
    uint32_t last_command_id;
    uint8_t last_command_result;
    uint8_t last_command_unit_id;
    uint8_t last_command_function;
    uint8_t last_command_exception;
    app_modbus_master_device_status_t
        devices[APP_DEVICE_CONFIG_MAX_MASTER_DEVICES];
} app_modbus_rtu_diagnostics_t;

HAL_StatusTypeDef app_modbus_rtu_start(void);
HAL_StatusTypeDef app_modbus_rtu_request_config(
    const app_modbus_rtu_config_t *config);
HAL_StatusTypeDef app_modbus_rtu_get_config(app_modbus_rtu_config_t *config);
HAL_StatusTypeDef app_modbus_rtu_queue_command(
    const app_modbus_command_t *command,
    uint32_t *command_id);
HAL_StatusTypeDef app_modbus_rtu_get_diagnostics(
    app_modbus_rtu_diagnostics_t *diagnostics);
HAL_StatusTypeDef app_modbus_rtu_request_h563_ota(
    uint8_t unit_id,
    uint32_t preferred_baud_rate);
void app_modbus_rtu_get_h563_ota_status(
    app_h563_ota_master_status_t *status);
const char *app_modbus_device_state_name(uint8_t state);
const char *app_modbus_command_result_name(uint8_t result);

#endif
