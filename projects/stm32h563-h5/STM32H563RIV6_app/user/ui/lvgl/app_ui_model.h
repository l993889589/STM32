/**
 * @file app_ui_model.h
 * @brief Coherent application diagnostics exposed to the LVGL presentation.
 */

#ifndef APP_UI_MODEL_H
#define APP_UI_MODEL_H

#include <stdint.h>

#include "app_can_self_test.h"
#include "app_self_test.h"
#include "app_w800.h"

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    uint8_t wifi_provisioning_active;
    uint32_t wifi_provision_attempts;
    uint32_t wifi_provision_timeouts;
    uint8_t wifi_usb_rescue_state;
    uint32_t wifi_usb_rescue_attempts;
    int w800_socket_id;
    uint8_t w800_state;
    uint8_t w800_mqtt_stage;
    uint16_t w800_socket_local_port;
    uint32_t w800_socket_rx_data;
    uint32_t w800_socket_recv_fail_count;
    uint32_t w800_rx_bytes;
    uint32_t w800_packets;
    app_w800_scan_snapshot_t wifi_scan;

    uint32_t console_rx_bytes;
    uint32_t console_packets;
    uint32_t console_drops;

    uint32_t rs485_rx_frames;
    uint32_t rs485_tx_frames;
    uint32_t rs485_ignored_frames;
    uint32_t rs485_crc_errors;
    uint32_t rs485_exceptions;
    uint32_t rs485_transport_errors;
    uint32_t rs485_rx_bytes;
    uint32_t rs485_packets;
    uint32_t modbus_server_requests;
    uint32_t modbus_server_responses;
    uint32_t modbus_master_passes;
    uint32_t modbus_master_failures;
    uint16_t modbus_last_register_0;
    uint16_t modbus_last_register_1;
    uint8_t modbus_unit_id;

    uint8_t usb_connected;
    uint32_t usb_frames;
    uint32_t usb_crc_errors;
    uint32_t usb_length_errors;
    uint32_t usb_discarded_bytes;
    uint32_t usb_rx_bytes;
    uint32_t usb_packets;

    uint8_t rtc_valid;
    uint16_t rtc_year;
    uint8_t rtc_month;
    uint8_t rtc_day;
    uint8_t rtc_hour;
    uint8_t rtc_minute;
    uint8_t rtc_second;
    uint16_t brightness_permille;

    uint8_t ota_active;
    uint32_t ota_received;
    uint32_t ota_expected;

    app_can_self_test_snapshot_t can_self_test;
    app_self_test_snapshot_t board_self_test;
} app_ui_model_snapshot_t;

/** @brief Copy one application diagnostics snapshot for the UI thread. */
void app_ui_model_get_snapshot(app_ui_model_snapshot_t *snapshot);

#endif /* APP_UI_MODEL_H */
