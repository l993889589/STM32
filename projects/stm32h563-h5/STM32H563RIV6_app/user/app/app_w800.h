/**
 * @file app_w800.h
 * @brief W800 AT/MQTT/HTTP service task state and diagnostic interface.
 */

#ifndef APP_W800_H
#define APP_W800_H

#include <stdbool.h>
#include <stdint.h>

#include "app_serial_stats.h"
#include "tx_api.h"

typedef enum
{
    APP_W800_USB_RESCUE_IDLE = 0,
    APP_W800_USB_RESCUE_PENDING,
    APP_W800_USB_RESCUE_APPLYING,
    APP_W800_USB_RESCUE_SAVED,
    APP_W800_USB_RESCUE_CONNECTED,
    APP_W800_USB_RESCUE_FAILED
} app_w800_usb_rescue_state_t;

typedef enum
{
    APP_W800_CREDENTIALS_ACCEPTED = 0,
    APP_W800_CREDENTIALS_INVALID_SSID,
    APP_W800_CREDENTIALS_INVALID_PASSWORD,
    APP_W800_CREDENTIALS_BUSY,
    APP_W800_CREDENTIALS_NOT_READY
} app_w800_credentials_result_t;

#define APP_W800_SCAN_MAX_RESULTS 8U

typedef enum
{
    APP_W800_SCAN_IDLE = 0,
    APP_W800_SCAN_PENDING,
    APP_W800_SCAN_RUNNING,
    APP_W800_SCAN_READY,
    APP_W800_SCAN_FAILED
} app_w800_scan_state_t;

typedef struct
{
    char ssid[33];
    int16_t rssi_dbm;
    uint8_t channel;
    uint8_t encryption;
} app_w800_access_point_t;

typedef struct
{
    app_w800_scan_state_t state;
    uint8_t count;
    uint32_t generation;
    app_w800_access_point_t access_points[APP_W800_SCAN_MAX_RESULTS];
} app_w800_scan_snapshot_t;

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    uint8_t provisioning_active;
    uint32_t provision_attempts;
    uint32_t provision_timeouts;
    uint8_t usb_rescue_state;
    uint32_t usb_rescue_attempts;
    int socket_id;
    uint8_t state;
    uint8_t mqtt_stage;
    uint16_t socket_local_port;
    uint32_t socket_rx_data;
    uint8_t socket_recv_result;
    uint16_t socket_recv_actual;
    uint32_t socket_recv_fail_count;
    uint8_t socket_recv_head[4];
    app_serial_stats_t ldc;
    uint32_t mqtt_publish_seen;
    uint32_t mqtt_begin_seen;
    uint32_t mqtt_chunk_seen;
    uint32_t mqtt_commit_seen;
    uint32_t mqtt_stream_drops;
    uint16_t mqtt_last_payload_len;
    char mqtt_last_topic[40];
    uint8_t http_pending;
    uint8_t http_active;
    uint8_t http_state;
    uint32_t http_received;
    uint32_t http_size;
    char http_error[24];
    uint8_t chunk_active;
    uint8_t chunk_state;
    uint32_t chunk_received;
    uint32_t chunk_size;
    uint16_t chunk_unit;
    uint8_t chunk_retry;
    uint32_t chunk_json_seen;
    uint32_t chunk_json_drop;
    uint32_t chunk_seq_error;
    uint32_t chunk_offset_error;
    uint32_t chunk_b64_error;
    uint32_t chunk_crc_error;
    char chunk_error[24];
} app_w800_status_t;

UINT app_w800_init(void);
void app_w800_task_entry(ULONG thread_input);
void app_w800_get_status(app_w800_status_t *status);
void app_w800_request_reconnect(void);
/** @brief Request exclusive W800 BLE provisioning from task context. */
void app_w800_request_ble_provisioning(void);
/**
 * @brief Queue WPA/WPA2 credentials captured by the masked USB rescue flow.
 * @note The credentials are copied into a single-use mailbox, consumed only by
 *       the W800 task, then explicitly wiped. They are never logged.
 */
app_w800_credentials_result_t app_w800_request_usb_credentials(const char *ssid,
                                                               const char *password);
/**
 * @brief Queue WPA/WPA2 credentials from any trusted local UI.
 * @note The implementation uses the same single-use, explicitly wiped mailbox
 *       as USB rescue. The credentials are never logged or retained by the UI.
 */
app_w800_credentials_result_t app_w800_request_credentials(const char *ssid,
                                                           const char *password);
/** @brief Queue one non-blocking nearby access-point scan. */
bool app_w800_request_scan(void);
/** @brief Copy the latest scan state and strongest-first access-point list. */
void app_w800_get_scan_snapshot(app_w800_scan_snapshot_t *snapshot);
/** @brief Apply one bounded JSON command through the MQTT command handler. */
bool app_w800_apply_remote_json(const char *json);
/** @brief Cooperatively park the W800 service at an idle UART boundary. */
bool app_w800_pause(uint32_t timeout_ms);
/** @brief Release a previously parked W800 service. */
void app_w800_resume(void);
const char *app_w800_mqtt_host(void);
uint16_t app_w800_mqtt_port(void);

#endif
