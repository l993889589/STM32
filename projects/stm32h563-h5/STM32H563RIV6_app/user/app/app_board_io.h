/**
 * @file app_board_io.h
 * @brief Application composition and board-service status interface.
 */

#ifndef APP_BOARD_IO_H
#define APP_BOARD_IO_H

#include <stdint.h>

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"
#include "app_modbus_types.h"
#include "ldc_core.h"

/** @brief Consolidated runtime status exported to Shell, USB, UI, and W800. */
typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    uint8_t ota_active;
    int w800_socket_id;
    uint8_t w800_state;
    uint8_t w800_mqtt_stage;
    uint16_t w800_socket_local_port;
    uint32_t w800_socket_rx_data;
    uint8_t w800_socket_recv_result;
    uint16_t w800_socket_recv_actual;
    uint32_t w800_socket_recv_fail_count;
    uint8_t w800_socket_recv_head[4];
    uint32_t w800_mqtt_publish_seen;
    uint32_t w800_mqtt_begin_seen;
    uint32_t w800_mqtt_chunk_seen;
    uint32_t w800_mqtt_commit_seen;
    uint32_t w800_mqtt_stream_drops;
    uint16_t w800_mqtt_last_payload_len;
    char w800_mqtt_last_topic[40];
    uint8_t w800_http_pending;
    uint8_t w800_http_active;
    uint8_t w800_http_state;
    uint32_t w800_http_received;
    uint32_t w800_http_size;
    char w800_http_error[24];
    uint8_t w800_chunk_active;
    uint8_t w800_chunk_state;
    uint32_t w800_chunk_received;
    uint32_t w800_chunk_size;
    uint16_t w800_chunk_unit;
    uint8_t w800_chunk_retry;
    uint32_t w800_chunk_json_seen;
    uint32_t w800_chunk_json_drop;
    uint32_t w800_chunk_seq_error;
    uint32_t w800_chunk_offset_error;
    uint32_t w800_chunk_b64_error;
    uint32_t w800_chunk_crc_error;
    char w800_chunk_error[24];
    uint32_t ota_received;
    uint32_t ota_expected;
    uint8_t vendor_connected;
    uint32_t vendor_frames;
    uint32_t vendor_crc_errors;
    uint32_t vendor_length_errors;
    uint32_t vendor_discarded_bytes;
    app_modbus_stats_t modbus;
    ldc_stats_t usb_ldc;
    ldc_stats_t rs485_ldc;
    ldc_stats_t w800_ldc;
} app_board_status_t;

/** @brief Initialize application-owned board services. */
UINT app_board_io_init(void);

/** @brief Bind an activated USB CDC ACM class instance. */
void app_usb_cdc_activate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);

/** @brief Release a deactivated USB CDC ACM class instance. */
void app_usb_cdc_deactivate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);

/** @brief Apply a USB CDC ACM line-parameter change notification. */
void app_usb_cdc_parameter_change(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);

/** @brief Return the currently active USB CDC ACM instance. */
UX_SLAVE_CLASS_CDC_ACM *app_usb_cdc_get(void);

/** @brief Forward received USB CDC bytes into the application service. */
void app_usb_cdc_process_rx(const uint8_t *data, uint32_t len);

/** @brief Run one bounded USB CDC application service step. */
void app_usb_cdc_service(void);

/** @brief Write a bounded byte sequence through the active USB CDC channel. */
UINT app_usb_cdc_write(const uint8_t *data, uint32_t len);

/** @brief Copy the consolidated application and board-service status. */
void app_board_get_status(app_board_status_t *status);

/** @brief Request a task-context MQTT reconnect. */
void app_board_request_mqtt_reconnect(void);

/** @brief Run the LED status task. */
void app_led_task_entry(ULONG thread_input);

/** @brief Run the RS-485 Modbus service task. */
void app_rs485_task_entry(ULONG thread_input);

/** @brief Run the W800 Wi-Fi and MQTT service task. */
void app_w800_task_entry(ULONG thread_input);

#endif
