#include "app_board_io.h"

#include <stdio.h>
#include <string.h>

#include "app_rs485.h"
#include "app_usb_service.h"
#include "app_w800.h"
#include "bsp.h"

#define APP_LED_TOGGLE_TICKS 1000U

static uint8_t g_initialized;

/** @brief Initialize BSP-backed board services once before application tasks start. */
UINT app_board_io_init(void)
{
    if(g_initialized != 0U)
        return TX_SUCCESS;

    if(bsp_init() != 0 ||
       app_usb_service_init() != TX_SUCCESS ||
       app_rs485_init() != TX_SUCCESS ||
       app_w800_init() != TX_SUCCESS
       )
        return TX_START_ERROR;

    g_initialized = 1U;
    return TX_SUCCESS;
}

void app_board_get_status(app_board_status_t *status)
{
    app_usb_service_status_t usb;
    app_w800_status_t w800;

    if(!status)
        return;

    app_usb_service_get_status(&usb);
    app_w800_get_status(&w800);
    status->wifi_ready = w800.wifi_ready;
    status->mqtt_online = w800.mqtt_online;
    status->ota_active = usb.ota_active;
    status->w800_socket_id = w800.socket_id;
    status->w800_state = w800.state;
    status->w800_mqtt_stage = w800.mqtt_stage;
    status->w800_socket_local_port = w800.socket_local_port;
    status->w800_socket_rx_data = w800.socket_rx_data;
    status->w800_socket_recv_result = w800.socket_recv_result;
    status->w800_socket_recv_actual = w800.socket_recv_actual;
    status->w800_socket_recv_fail_count = w800.socket_recv_fail_count;
    memcpy(status->w800_socket_recv_head,
           w800.socket_recv_head,
           sizeof(status->w800_socket_recv_head));
    status->w800_mqtt_publish_seen = w800.mqtt_publish_seen;
    status->w800_mqtt_begin_seen = w800.mqtt_begin_seen;
    status->w800_mqtt_chunk_seen = w800.mqtt_chunk_seen;
    status->w800_mqtt_commit_seen = w800.mqtt_commit_seen;
    status->w800_mqtt_stream_drops = w800.mqtt_stream_drops;
    status->w800_mqtt_last_payload_len = w800.mqtt_last_payload_len;
    (void)strncpy(status->w800_mqtt_last_topic, w800.mqtt_last_topic, sizeof(status->w800_mqtt_last_topic) - 1U);
    status->w800_mqtt_last_topic[sizeof(status->w800_mqtt_last_topic) - 1U] = '\0';
    status->w800_http_pending = w800.http_pending;
    status->w800_http_active = w800.http_active;
    status->w800_http_state = w800.http_state;
    status->w800_http_received = w800.http_received;
    status->w800_http_size = w800.http_size;
    (void)strncpy(status->w800_http_error, w800.http_error, sizeof(status->w800_http_error) - 1U);
    status->w800_http_error[sizeof(status->w800_http_error) - 1U] = '\0';
    status->w800_chunk_active = w800.chunk_active;
    status->w800_chunk_state = w800.chunk_state;
    status->w800_chunk_received = w800.chunk_received;
    status->w800_chunk_size = w800.chunk_size;
    status->w800_chunk_unit = w800.chunk_unit;
    status->w800_chunk_retry = w800.chunk_retry;
    status->w800_chunk_json_seen = w800.chunk_json_seen;
    status->w800_chunk_json_drop = w800.chunk_json_drop;
    status->w800_chunk_seq_error = w800.chunk_seq_error;
    status->w800_chunk_offset_error = w800.chunk_offset_error;
    status->w800_chunk_b64_error = w800.chunk_b64_error;
    status->w800_chunk_crc_error = w800.chunk_crc_error;
    (void)strncpy(status->w800_chunk_error, w800.chunk_error, sizeof(status->w800_chunk_error) - 1U);
    status->w800_chunk_error[sizeof(status->w800_chunk_error) - 1U] = '\0';
    status->ota_received = usb.ota_received;
    status->ota_expected = usb.ota_expected;
    status->vendor_connected = usb.vendor_connected;
    status->vendor_frames = usb.vendor_frames;
    status->vendor_crc_errors = usb.vendor_crc_errors;
    status->vendor_length_errors = usb.vendor_length_errors;
    status->vendor_discarded_bytes = usb.vendor_discarded_bytes;
    status->usb_ldc = usb.ldc;
    status->w800_ldc = w800.ldc;
    app_rs485_get_stats(&status->modbus);
    (void)app_rs485_get_ldc_stats(&status->rs485_ldc);
}

void app_board_request_mqtt_reconnect(void)
{
    app_w800_request_reconnect();
}

void app_led_task_entry(ULONG thread_input)
{
    (void)thread_input;
    for(;;)
    {
        bsp_led_toggle(BSP_LED_STATUS);
        tx_thread_sleep(APP_LED_TOGGLE_TICKS);
    }
}
