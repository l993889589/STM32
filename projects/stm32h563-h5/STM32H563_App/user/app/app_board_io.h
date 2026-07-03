#ifndef APP_BOARD_IO_H
#define APP_BOARD_IO_H

#include <stdint.h>

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"
#include "modbus.h"
#include "ldc_core.h"

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    uint8_t ota_active;
    int w800_socket_id;
    uint32_t ota_received;
    uint32_t ota_expected;
    uint8_t vendor_connected;
    uint32_t vendor_frames;
    uint32_t vendor_crc_errors;
    uint32_t vendor_length_errors;
    uint32_t vendor_discarded_bytes;
    modbus_stats_t modbus;
    ldc_stats_t usb_ldc;
    ldc_stats_t rs485_ldc;
    ldc_stats_t w800_ldc;
} app_board_status_t;

UINT app_board_io_init(void);
void app_usb_cdc_activate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);
void app_usb_cdc_deactivate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);
void app_usb_cdc_parameter_change(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);
UX_SLAVE_CLASS_CDC_ACM *app_usb_cdc_get(void);
void app_usb_cdc_process_rx(const uint8_t *data, uint32_t len);
void app_usb_cdc_service(void);
UINT app_usb_cdc_write(const uint8_t *data, uint32_t len);
void app_board_get_status(app_board_status_t *status);
void app_board_request_mqtt_reconnect(void);
void app_led_task_entry(ULONG thread_input);
void app_rs485_task_entry(ULONG thread_input);
void app_w800_task_entry(ULONG thread_input);

#endif
