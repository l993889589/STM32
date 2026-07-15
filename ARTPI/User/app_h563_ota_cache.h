#ifndef APP_H563_OTA_CACHE_H
#define APP_H563_OTA_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"
#include "ota_modbus_protocol.h"

#define APP_H563_OTA_MANIFEST_SIZE 188U

typedef enum
{
    APP_H563_OTA_CACHE_EMPTY = 0,
    APP_H563_OTA_CACHE_RECEIVING = 1,
    APP_H563_OTA_CACHE_VERIFYING = 2,
    APP_H563_OTA_CACHE_READY = 3,
    APP_H563_OTA_CACHE_TRANSFERRING = 4,
    APP_H563_OTA_CACHE_ERROR = 5
} app_h563_ota_cache_state_t;

typedef struct
{
    uint8_t state;
    uint8_t last_error;
    uint16_t reserved;
    uint32_t received_size;
    uint32_t expected_size;
    uint32_t image_version;
    uint32_t image_crc32;
} app_h563_ota_cache_status_t;

HAL_StatusTypeDef app_h563_ota_cache_init(void);
HAL_StatusTypeDef app_h563_ota_cache_write_manifest(const uint8_t *manifest,
                                                     size_t length);
HAL_StatusTypeDef app_h563_ota_cache_write_image(uint32_t offset,
                                                  const uint8_t *data,
                                                  size_t length);
HAL_StatusTypeDef app_h563_ota_cache_finish(void);
HAL_StatusTypeDef app_h563_ota_cache_get_descriptor(
    ota_modbus_descriptor_t *descriptor);
HAL_StatusTypeDef app_h563_ota_cache_read_image(uint32_t offset,
                                                 uint8_t *data,
                                                 size_t length);
HAL_StatusTypeDef app_h563_ota_cache_set_transferring(uint8_t active);
void app_h563_ota_cache_get_status(app_h563_ota_cache_status_t *status);
const char *app_h563_ota_cache_state_name(uint8_t state);

#endif
