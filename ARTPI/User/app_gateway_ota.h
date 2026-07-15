#ifndef APP_GATEWAY_OTA_H
#define APP_GATEWAY_OTA_H

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

typedef enum
{
    APP_GATEWAY_OTA_EMPTY = 0,
    APP_GATEWAY_OTA_RECEIVING = 1,
    APP_GATEWAY_OTA_VERIFYING = 2,
    APP_GATEWAY_OTA_READY = 3,
    APP_GATEWAY_OTA_REQUESTED = 4,
    APP_GATEWAY_OTA_CONSUMED = 5,
    APP_GATEWAY_OTA_ERROR = 6
} app_gateway_ota_state_t;

typedef struct
{
    uint8_t state;
    uint8_t last_error;
    uint8_t install_requested;
    uint8_t consumed;
    uint32_t received_size;
    uint32_t expected_size;
    uint32_t image_version;
    uint32_t image_crc32;
    uint32_t boot_error;
    uint32_t active_version;
    uint32_t pending_version;
    uint32_t boot_sequence;
} app_gateway_ota_status_t;

HAL_StatusTypeDef app_gateway_ota_init(void);
HAL_StatusTypeDef app_gateway_ota_write_manifest(const uint8_t *manifest,
                                                  size_t length);
HAL_StatusTypeDef app_gateway_ota_write_image(uint32_t offset,
                                               const uint8_t *data,
                                               size_t length);
HAL_StatusTypeDef app_gateway_ota_finish(void);
HAL_StatusTypeDef app_gateway_ota_request_install(void);
void app_gateway_ota_get_status(app_gateway_ota_status_t *status);
const char *app_gateway_ota_state_name(uint8_t state);

#endif
