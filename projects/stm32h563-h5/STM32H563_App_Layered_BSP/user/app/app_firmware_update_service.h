/*
 * Application binding for the shared external firmware A/B transaction.
 *
 * Initialize once after GD25LQ128 startup. USB and HTTP transports then call
 * begin/write/finish with image metadata and slot-relative data offsets.
 */
#ifndef APP_FIRMWARE_UPDATE_SERVICE_H
#define APP_FIRMWARE_UPDATE_SERVICE_H

#include "ota_firmware_update.h"
#include <stdint.h>

ota_firmware_update_status_t app_firmware_update_service_init(void);
ota_firmware_update_status_t app_firmware_update_service_begin(
    const ota_firmware_descriptor_t *descriptor);
ota_firmware_update_status_t app_firmware_update_service_write(
    uint32_t offset,
    const uint8_t *data,
    uint32_t size);
ota_firmware_update_status_t app_firmware_update_service_finish(void);
ota_firmware_update_status_t app_firmware_update_service_abort(void);
void app_firmware_update_service_get_progress(
    uint8_t *is_active,
    uint32_t *target_slot,
    uint32_t *received_size,
    uint32_t *expected_size);

#endif /* APP_FIRMWARE_UPDATE_SERVICE_H */
