#ifndef APP_W800_H
#define APP_W800_H

#include <stdbool.h>
#include <stdint.h>

#include "tx_api.h"
#include "ldc_core.h"

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
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
/** @brief Apply one bounded JSON command through the MQTT command handler. */
bool app_w800_apply_remote_json(const char *json);
/** @brief Cooperatively park the W800 service at an idle UART boundary. */
bool app_w800_pause(uint32_t timeout_ms);
/** @brief Release a previously parked W800 service. */
void app_w800_resume(void);
const char *app_w800_wifi_ssid(void);
const char *app_w800_mqtt_host(void);
uint16_t app_w800_mqtt_port(void);

#endif
