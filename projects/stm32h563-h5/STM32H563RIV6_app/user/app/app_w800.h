#ifndef APP_W800_H
#define APP_W800_H

#include <stdbool.h>
#include <stdint.h>

#include "tx_api.h"
#include "ldc_core.h"

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
    ldc_stats_t ldc;
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
/** @brief Apply one bounded JSON command through the MQTT command handler. */
bool app_w800_apply_remote_json(const char *json);
/** @brief Cooperatively park the W800 service at an idle UART boundary. */
bool app_w800_pause(uint32_t timeout_ms);
/** @brief Release a previously parked W800 service. */
void app_w800_resume(void);
const char *app_w800_mqtt_host(void);
uint16_t app_w800_mqtt_port(void);

#endif
