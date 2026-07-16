#ifndef APP_H563_OTA_MASTER_H
#define APP_H563_OTA_MASTER_H

#include <stdint.h>

#include "ld_modbus_rtu_framer.h"
#include "stm32h7xx_hal.h"

typedef enum
{
    APP_H563_OTA_PHASE_IDLE = 0,
    APP_H563_OTA_PHASE_QUEUED,
    APP_H563_OTA_PHASE_HELLO,
    APP_H563_OTA_PHASE_BAUD_TEST,
    APP_H563_OTA_PHASE_BEGIN,
    APP_H563_OTA_PHASE_TRANSFER,
    APP_H563_OTA_PHASE_VERIFY,
    APP_H563_OTA_PHASE_ACTIVATE,
    APP_H563_OTA_PHASE_COMPLETE,
    APP_H563_OTA_PHASE_FAILED
} app_h563_ota_phase_t;

typedef enum
{
    APP_H563_OTA_RESULT_NONE = 0,
    APP_H563_OTA_RESULT_OK,
    APP_H563_OTA_RESULT_CACHE_NOT_READY,
    APP_H563_OTA_RESULT_TRANSPORT,
    APP_H563_OTA_RESULT_PROTOCOL,
    APP_H563_OTA_RESULT_REMOTE,
    APP_H563_OTA_RESULT_BAUD,
    APP_H563_OTA_RESULT_BUSY
} app_h563_ota_result_t;

typedef struct
{
    uint8_t phase;
    uint8_t result;
    uint8_t unit_id;
    uint8_t last_remote_status;
    uint32_t session_id;
    uint32_t requested_baud_rate;
    uint32_t active_baud_rate;
    uint32_t transferred_size;
    uint32_t image_size;
    uint32_t retry_count;
    uint32_t started_ms;
    uint32_t finished_ms;
} app_h563_ota_master_status_t;

HAL_StatusTypeDef app_h563_ota_master_init(
    ld_modbus_rtu_framer_t *receiver,
    uint8_t bits_per_char,
    uint32_t timestamp_hz);
HAL_StatusTypeDef app_h563_ota_master_request(uint8_t unit_id,
                                              uint32_t preferred_baud_rate);
uint8_t app_h563_ota_master_has_pending(void);
void app_h563_ota_master_run_pending(void);
void app_h563_ota_master_get_status(app_h563_ota_master_status_t *status);
const char *app_h563_ota_phase_name(uint8_t phase);
const char *app_h563_ota_result_name(uint8_t result);

#endif
