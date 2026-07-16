/**
 * @file app_usb_service.h
 * @brief ThreadX USB console/vendor service ownership and task interface.
 */

#ifndef APP_USB_SERVICE_H
#define APP_USB_SERVICE_H

#include <stdint.h>

#include "app_serial_stats.h"
#include "tx_api.h"

typedef struct
{
    uint8_t ota_active;
    uint8_t vendor_connected;
    uint8_t cdc_connected;
    uint32_t ota_received;
    uint32_t ota_expected;
    uint32_t vendor_frames;
    uint32_t vendor_crc_errors;
    uint32_t vendor_length_errors;
    uint32_t vendor_discarded_bytes;
    app_serial_stats_t ldc;
} app_usb_service_status_t;

UINT app_usb_service_init(void);
void app_usb_service_get_status(app_usb_service_status_t *status);

/** @brief Write a bounded byte sequence through the active USB CDC channel. */
UINT app_usb_cdc_write(const uint8_t *data, uint32_t len);

#endif
