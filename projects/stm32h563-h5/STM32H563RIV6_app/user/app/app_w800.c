/**
 * @file app_w800.c
 * @brief W800 AT, MQTT, HTTP asset update, and health service.
 */

#include "app_w800.h"

#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_board_io.h"
#include "app_blackbox.h"
#include "app_can_self_test.h"
#include "app_config.h"
#include "app_firmware_update_service.h"
#include "app_health.h"
#include "app_power.h"
#include "app_production_test.h"
#include "app_rs485.h"
#include "app_self_test.h"
#include "app_w800_config.h"
#include "at_module.h"
#include "at_module_w800.h"
#include "at_session.h"
#include "bsp.h"
#include "ldc_easy.h"
#include "ldc_port_irq.h"
#include "mqtt_packet.h"
#include "ota_layout.h"
#include "ui_asset_store.h"
#include "usb_console.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

#define APP_W800_LDC_MAX_FRAME       1280U
#define APP_W800_LDC_IDLE_TIMEOUT_MS 3U
#define APP_ENABLE_W800_LOG          0U
#define APP_W800_RX_BUF_SIZE         4096U
#define APP_W800_UART_RX_BUF_SIZE    256U
#define APP_W800_PACKET_COUNT        24U
#define APP_W800_TX_TIMEOUT_MS       1000U
#define APP_W800_CMD_TIMEOUT_MS      3000U
#define APP_W800_JOIN_TIMEOUT_MS     45000U
#define APP_W800_RESTORE_TIMEOUT_MS  15000U
#define APP_W800_PROVISION_TIMEOUT_MS 300000U
#define APP_W800_PROVISION_POLL_MS   1000U
#define APP_W800_PROVISION_RETRY_MS  3000U
#define APP_W800_SSID_MAX_LENGTH     32U
#define APP_W800_PASSWORD_MAX_LENGTH 63U
#define APP_W800_MQTT_KEEPALIVE_S    1800U
#define APP_W800_MQTT_HTTP_PING_MS   0xFFFFFFFFUL
#define APP_W800_MQTT_CLIENT_ID      "leduo-h563-w800"
#define APP_W800_FW_BUILD_ID         __DATE__ " " __TIME__
#define APP_W800_MQTT_STATUS_TOPIC   "leduo/w800/status"
#define APP_W800_MQTT_DIAG_TOPIC     "leduo/w800/diag"
#define APP_W800_MQTT_OPS_TOPIC      "leduo/w800/ops"
#define APP_W800_MQTT_CONFIG_TOPIC   "leduo/w800/config"
#define APP_W800_MQTT_DATA_TOPIC     "leduo/w800/modbus/data"
#define APP_W800_MQTT_CMD_TOPIC      "leduo/w800/cmd"
#define APP_W800_MQTT_UI_WILDCARD_TOPIC "leduo/w800/ui/#"
#define APP_W800_MQTT_UI_BEGIN_TOPIC "leduo/w800/ui/begin"
#define APP_W800_MQTT_UI_CHUNK_TOPIC "leduo/w800/ui/chunk"
#define APP_W800_MQTT_UI_COMMIT_TOPIC "leduo/w800/ui/commit"
#define APP_W800_MQTT_UI_REQ_TOPIC   "leduo/w800/ui/req"
#define APP_W800_LOCAL_PORT_START    10000U
#define APP_W800_LOCAL_PORT_END      18999U
#define APP_W800_HTTP_LOCAL_PORT_START 20000U
#define APP_W800_HTTP_LOCAL_PORT_END   60999U
#define APP_W800_MQTT_RX_STREAM_SIZE 2048U
#define APP_W800_MQTT_IDLE_FETCH_MS  50U
#define APP_W800_MQTT_ACTIVE_FETCH_MS 20U
#define APP_W800_MQTT_HEARTBEAT_MS   5000U
#define APP_W800_HTTP_RECV_CHUNK     1400U
#define APP_W800_HTTP_RECV_WAIT_MS   3000U
#define APP_W800_TCP_CHUNK_DEFAULT   512U
#define APP_W800_TCP_CHUNK_HEADER_MAX 96U
#define APP_W800_HTTP_MODE_FULL      0U
#define APP_W800_HTTP_MODE_RAW       1U
#define APP_W800_HTTP_MODE_TCP_CHUNK 2U
#define APP_W800_HTTP_MODE_MANIFEST  3U
#define APP_W800_HTTP_MODE_RANGE     4U
#define APP_W800_HTTP_RANGE_DEFAULT  4096U
#define APP_W800_HTTP_RANGE_MIN      64U
#define APP_W800_HTTP_RANGE_MAX      4096U
#define APP_W800_HTTP_RANGE_RETRY_LIMIT 8U
#define APP_W800_UI_CHUNK_DEFAULT    512U
#define APP_W800_UI_CHUNK_MAX        512U
#define APP_W800_UI_CHUNK_TIMEOUT_MS 5000U
#define APP_W800_UI_CHUNK_MAX_RETRY  5U

typedef enum
{
    APP_W800_STATE_RESET = 0,
    APP_W800_STATE_WIFI_RESTORE,
    APP_W800_STATE_PROVISION_START,
    APP_W800_STATE_PROVISION_WAIT,
    APP_W800_STATE_MQTT_SOCKET,
    APP_W800_STATE_MQTT_CONNECT,
    APP_W800_STATE_ONLINE,
    APP_W800_STATE_MQTT_RETRY
} app_w800_state_t;

static ldc_easy_t g_ldc;
static uint8_t g_ldc_ring[APP_W800_RX_BUF_SIZE];
static ldc_packet_t g_packets[APP_W800_PACKET_COUNT];
static BSP_ALIGN_32(uint8_t g_uart_rx_buffer[APP_W800_UART_RX_BUF_SIZE]);
static TX_SEMAPHORE g_rx_sem;
static at_session_t g_at_session;
static at_module_t g_module;
static uint16_t g_local_port = APP_W800_LOCAL_PORT_START;
static uint16_t g_http_local_port = APP_W800_HTTP_LOCAL_PORT_START;
static volatile uint8_t g_wifi_ready;
static volatile uint8_t g_mqtt_online;
static volatile uint8_t g_reconnect_requested;
static volatile uint8_t g_provision_requested;
static volatile uint8_t g_provisioning_active;
static volatile uint32_t g_provision_attempts;
static volatile uint32_t g_provision_timeouts;
static volatile uint8_t g_usb_rescue_state;
static volatile uint32_t g_usb_rescue_attempts;
static TX_MUTEX g_usb_credentials_mutex;
static uint8_t g_usb_credentials_mutex_ready;
static uint8_t g_usb_credentials_pending;
static char g_usb_ssid[APP_W800_SSID_MAX_LENGTH + 1U];
static char g_usb_password[APP_W800_PASSWORD_MAX_LENGTH + 1U];
static volatile uint8_t g_publish_status_requested;
static volatile uint8_t g_publish_config_requested;
static volatile uint8_t g_publish_data_requested;
static volatile uint8_t g_publish_ops_requested;
static volatile uint8_t g_remote_self_test_watch;
static volatile uint8_t g_remote_production_watch;
static volatile uint32_t g_remote_self_test_generation;
static volatile uint8_t g_state_diag;
static volatile uint8_t g_mqtt_stage;
static volatile uint16_t g_socket_local_port;
static volatile uint32_t g_socket_rx_data;
static volatile uint8_t g_socket_recv_result;
static volatile uint16_t g_socket_recv_actual;
static volatile uint32_t g_socket_recv_fail_count;
static volatile uint8_t g_socket_recv_head[4];
static uint16_t g_mqtt_packet_id = 1U;
static uint8_t g_input_frame[APP_W800_LDC_MAX_FRAME];
static char g_network_command[1024];
static uint8_t g_mqtt_connect_packet[128];
static uint8_t g_mqtt_subscribe_packet[160];
#if APP_W800_STATUS_MQTT_ENABLE
static uint8_t g_mqtt_status_packet[2048];
static char g_mqtt_status_payload[1536];
#endif
static uint8_t g_mqtt_modbus_packet[1024];
static char g_mqtt_modbus_payload[768];
static uint8_t g_mqtt_diag_packet[384];
static char g_mqtt_diag_payload[256];
static uint8_t g_mqtt_ops_packet[2048];
static char g_mqtt_ops_payload[1536];
static uint8_t g_mqtt_ui_req_packet[384];
static char g_mqtt_ui_req_payload[192];
static uint8_t g_mqtt_rx_stream[APP_W800_MQTT_RX_STREAM_SIZE];
static uint16_t g_mqtt_rx_len;
static uint32_t g_ui_status_next_report;
static volatile uint32_t g_mqtt_packet_seen;
static volatile uint32_t g_mqtt_publish_seen;
static volatile uint32_t g_mqtt_begin_seen;
static volatile uint32_t g_mqtt_chunk_seen;
static volatile uint32_t g_mqtt_commit_seen;
static volatile uint32_t g_mqtt_suback_seen;
static volatile uint32_t g_mqtt_pingresp_seen;
static volatile uint32_t g_mqtt_stream_drops;
static volatile uint8_t g_mqtt_last_packet_type;
static volatile uint16_t g_mqtt_last_payload_len;
static volatile uint16_t g_mqtt_last_packet_len;
static volatile uint16_t g_mqtt_last_fixed_header_len;
static volatile uint16_t g_mqtt_last_topic_len;
static volatile uint8_t g_mqtt_last_publish_reason;
static volatile uint8_t g_mqtt_last_b0;
static volatile uint8_t g_mqtt_last_b1;
static volatile uint8_t g_mqtt_last_b2;
static volatile uint8_t g_mqtt_last_b3;
static volatile uint8_t g_mqtt_last_b4;
static volatile uint8_t g_mqtt_last_b5;
static uint32_t g_mqtt_diag_last_ms;
static char g_mqtt_last_topic[40];

static ULONG app_w800_ms_to_ticks(uint32_t milliseconds);
static uint16_t app_w800_http_next_local_port(void);
static bool app_w800_mqtt_puback(uint16_t packet_id);
static bool app_w800_mqtt_publish_status(const char *mode);
static bool app_w800_mqtt_publish_diag(const char *where);
static bool app_w800_mqtt_publish_ops(uint8_t request);

/** @brief Remote operations report selected by one command request. */
typedef enum
{
    APP_W800_OPS_NONE = 0,
    APP_W800_OPS_DIAGNOSTICS,
    APP_W800_OPS_BLACKBOX,
    APP_W800_OPS_PRODUCTION
} app_w800_ops_request_t;

typedef enum
{
    APP_W800_HTTP_IDLE = 0,
    APP_W800_HTTP_HEADER,
    APP_W800_HTTP_BODY,
    APP_W800_HTTP_DONE,
    APP_W800_HTTP_ERROR
} app_w800_http_state_t;

typedef enum
{
    APP_W800_HTTP_TARGET_UI = 0,
    APP_W800_HTTP_TARGET_FIRMWARE = 1
} app_w800_http_target_t;

typedef struct
{
    char host[48];
    char path[80];
    uint16_t port;
    uint32_t size;
    uint32_t version;
    uint32_t crc32;
    uint32_t image_flags;
    uint32_t entry_address;
    uint8_t image_sha256[32];
    uint8_t signature[64];
    uint32_t received;
    uint32_t calc_crc32;
    uint32_t content_length;
    uint32_t range_offset;
    uint32_t range_length;
    uint32_t range_received;
    uint32_t range_crc32;
    uint32_t range_calc_crc32;
    uint8_t pending;
    uint8_t active;
    uint8_t raw_tcp;
    uint8_t target;
    app_w800_http_state_t state;
    char error[24];
    char header[512];
    uint16_t header_len;
} app_w800_http_update_t;

static app_w800_http_update_t g_http_update;
static char g_http_manifest[512];
static uint16_t g_http_manifest_len;
static uint8_t g_http_range_buffer[APP_W800_HTTP_RANGE_MAX];
static uint8_t g_socket_rx_payload[APP_W800_HTTP_RECV_CHUNK];

static int g_http_socket_id = -1;
static uint32_t g_http_debug_ok_count;
static uint32_t g_http_debug_ok_bytes;
static uint32_t g_http_debug_payload_bytes;
static uint32_t g_http_debug_stt_count;
static uint32_t g_http_debug_stt_rx_max;
static uint16_t g_http_debug_header_len;
static uint32_t g_http_direct_chunk_size = APP_W800_HTTP_RANGE_DEFAULT;
/* Set only after the inactive firmware slot is fully verified and pending. */
static uint8_t g_firmware_reboot_pending;

typedef struct
{
    uint32_t offset;
    uint32_t length;
    uint32_t seq;
    uint32_t crc32;
    uint32_t calc_crc32;
    uint16_t remaining;
    uint8_t waiting;
    uint8_t header_done;
    char header[APP_W800_TCP_CHUNK_HEADER_MAX];
    uint16_t header_len;
} app_w800_tcp_chunk_frame_t;

static app_w800_tcp_chunk_frame_t g_tcp_chunk_frame;

typedef enum
{
    APP_W800_CHUNK_IDLE = 0,
    APP_W800_CHUNK_REQUEST,
    APP_W800_CHUNK_WAIT,
    APP_W800_CHUNK_DONE,
    APP_W800_CHUNK_ERROR
} app_w800_chunk_state_t;

typedef struct
{
    uint32_t size;
    uint32_t version;
    uint32_t crc32;
    uint32_t received;
    uint32_t calc_crc32;
    uint32_t last_request_ms;
    uint32_t last_progress_ms;
    uint16_t chunk_size;
    uint16_t seq;
    uint32_t json_seen;
    uint32_t json_drop;
    uint32_t seq_error;
    uint32_t offset_error;
    uint32_t b64_error;
    uint32_t crc_error;
    uint8_t active;
    uint8_t retry;
    app_w800_chunk_state_t state;
    char error[24];
} app_w800_chunk_update_t;

static app_w800_chunk_update_t g_chunk_update;
static char g_chunk_base64[700];
static uint8_t g_chunk_data[APP_W800_UI_CHUNK_MAX];

static void app_w800_fetch_socket_input(void);
static void app_w800_poll_input(void *arg);
static void app_w800_ui_chunk_step(uint32_t now);
static uint16_t app_w800_next_local_port(void);
static void app_w800_clear_at_rx_state(void);
static void app_w800_http_reset_stream_state(void);
static void app_w800_scan_json_command_bytes(const uint8_t *data, uint16_t length);

static void app_w800_secure_zero(void *data, size_t length)
{
    volatile uint8_t *cursor = (volatile uint8_t *)data;

    while(length-- != 0U)
        *cursor++ = 0U;
}

static bool app_w800_credential_is_valid(const char *text,
                                         size_t min_length,
                                         size_t max_length)
{
    size_t length;
    size_t i;

    if(text == NULL)
        return false;
    length = strlen(text);
    if(length < min_length || length > max_length)
        return false;

    for(i = 0U; i < length; i++)
    {
        const unsigned char ch = (unsigned char)text[i];

        if(ch < 0x20U || ch > 0x7EU || ch == (unsigned char)'"')
            return false;
    }
    return true;
}

static bool app_w800_take_usb_credentials(char *ssid, char *password)
{
    bool available = false;

    if(g_usb_credentials_mutex_ready == 0U || ssid == NULL || password == NULL)
        return false;
    if(tx_mutex_get(&g_usb_credentials_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        return false;

    if(g_usb_credentials_pending != 0U)
    {
        memcpy(ssid, g_usb_ssid, sizeof(g_usb_ssid));
        memcpy(password, g_usb_password, sizeof(g_usb_password));
        app_w800_secure_zero(g_usb_ssid, sizeof(g_usb_ssid));
        app_w800_secure_zero(g_usb_password, sizeof(g_usb_password));
        g_usb_credentials_pending = 0U;
        g_usb_rescue_state = APP_W800_USB_RESCUE_APPLYING;
        available = true;
    }

    (void)tx_mutex_put(&g_usb_credentials_mutex);
    return available;
}

static void app_w800_log_line(const char *line)
{
#if APP_ENABLE_W800_LOG
    if(line)
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
#else
    (void)line;
#endif
}

static void app_w800_log_token(const char *line)
{
#if APP_ENABLE_W800_LOG
    char output[192];
    int length;

    if(!line)
        return;
    length = snprintf(output, sizeof(output), "%s\r\n", line);
    if(length > 0)
        (void)app_usb_cdc_write((const uint8_t *)output, (uint32_t)length);
#else
    (void)line;
#endif
}

static void app_w800_at_log(const char *line, void *arg)
{
    char tagged[192];

    (void)arg;
    if(!line)
        return;
    (void)snprintf(tagged, sizeof(tagged), "[w800] %s", line);
    app_w800_log_token(tagged);
}

static uint32_t app_w800_now_ms(void *arg)
{
    uint64_t ticks;

    (void)arg;
    ticks = tx_time_get();
    return (uint32_t)((ticks * 1000ULL) / (uint64_t)TX_TIMER_TICKS_PER_SECOND);
}

static uint32_t app_w800_get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t app_w800_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    while(len-- != 0U)
    {
        crc ^= *data++;
        for(uint32_t bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }
    return ~crc;
}

static void app_w800_chunk_set_error(const char *error)
{
    if(error == NULL)
        error = "unknown";

    (void)strncpy(g_chunk_update.error, error, sizeof(g_chunk_update.error) - 1U);
    g_chunk_update.error[sizeof(g_chunk_update.error) - 1U] = '\0';
    g_chunk_update.state = APP_W800_CHUNK_ERROR;
    g_chunk_update.active = 0U;
    g_publish_status_requested = 1U;
}

static int8_t app_w800_base64_value(char ch)
{
    if(ch >= 'A' && ch <= 'Z')
        return (int8_t)(ch - 'A');
    if(ch >= 'a' && ch <= 'z')
        return (int8_t)(ch - 'a' + 26);
    if(ch >= '0' && ch <= '9')
        return (int8_t)(ch - '0' + 52);
    if(ch == '+')
        return 62;
    if(ch == '/')
        return 63;
    if(ch == '=')
        return -2;
    return -1;
}

static bool app_w800_base64_decode(const char *text,
                                   uint8_t *out,
                                   uint16_t out_size,
                                   uint16_t *out_len)
{
    uint32_t accum = 0U;
    uint8_t bits = 0U;
    uint16_t written = 0U;

    if(!text || !out || !out_len)
        return false;

    while(*text != '\0')
    {
        int8_t value = app_w800_base64_value(*text++);

        if(value == -2)
            break;
        if(value < 0)
            return false;

        accum = (accum << 6) | (uint32_t)value;
        bits = (uint8_t)(bits + 6U);
        if(bits >= 8U)
        {
            bits = (uint8_t)(bits - 8U);
            if(written >= out_size)
                return false;
            out[written++] = (uint8_t)((accum >> bits) & 0xFFU);
        }
    }

    *out_len = written;
    return true;
}

static void app_w800_http_set_error(const char *error)
{
    if(error == NULL)
        error = "unknown";
    (void)strncpy(g_http_update.error, error, sizeof(g_http_update.error) - 1U);
    g_http_update.error[sizeof(g_http_update.error) - 1U] = '\0';
    g_http_update.state = APP_W800_HTTP_ERROR;
    g_http_update.active = 0U;
}

static int app_w800_parse_socket_id(const char *text)
{
    const char *ok;

    if(!text)
        return -1;

    ok = strstr(text, "+OK=");
    if(!ok)
        return -1;
    ok += 4;
    if(*ok < '0' || *ok > '9')
        return -1;

    return (int)strtol(ok, NULL, 0);
}

static uint32_t app_w800_parse_socket_rx_data(const char *text)
{
    const char *line;
    const char *last_comma;

    if(text == NULL)
        return 0U;

    line = strstr(text, "+OK=");
    if(line == NULL)
        return 0U;

    last_comma = strrchr(line, ',');
    if(last_comma == NULL)
        return 0U;

    return (uint32_t)strtoul(last_comma + 1, NULL, 0);
}

static uint16_t app_w800_parse_socket_local_port(const char *text)
{
    const char *line;
    const char *last_comma;
    const char *port_comma;

    if(text == NULL)
        return 0U;

    line = strstr(text, "+OK=");
    if(line == NULL)
        return 0U;

    last_comma = strrchr(line, ',');
    if(last_comma == NULL || last_comma == line)
        return 0U;

    port_comma = last_comma - 1;
    while(port_comma > line && *port_comma != ',')
        port_comma--;
    if(*port_comma != ',')
        return 0U;

    return (uint16_t)strtoul(port_comma + 1, NULL, 0);
}

static bool app_w800_wait_socket_rx_data(int socket_id, uint32_t timeout_ms)
{
    char command[32];
    uint32_t start_ms;
    uint32_t rx_data;

    if(socket_id < 0)
        return false;

    start_ms = app_w800_now_ms(NULL);
    while((uint32_t)(app_w800_now_ms(NULL) - start_ms) < timeout_ms)
    {
        (void)snprintf(command, sizeof(command), "AT+SKSTT=%d", socket_id);
        app_w800_clear_at_rx_state();
        if(at_session_cmd_expect(&g_at_session, command, "+OK=", 1000U, 1U))
        {
            const char *capture = at_session_capture(&g_at_session);
            g_http_debug_stt_count++;
            rx_data = app_w800_parse_socket_rx_data(capture);
            g_socket_local_port = app_w800_parse_socket_local_port(capture);
            g_socket_rx_data = rx_data;
            if(rx_data > g_http_debug_stt_rx_max)
                g_http_debug_stt_rx_max = rx_data;
            if(rx_data != 0U)
                return true;
        }
        tx_thread_sleep(app_w800_ms_to_ticks(100U));
    }

    return false;
}

/* app_w800_socket_recv_when_ready
 *
 * Queries W800 SKSTT before issuing SKRCV. The module uses one AT UART for all
 * sockets, so blindly sending SKRCV when rx_data is zero can create timeout
 * noise that is later parsed as another command's response. Call this only from
 * the W800 task context while owning the shared AT session.
 */
static bool app_w800_socket_recv_when_ready(int socket_id,
                                            uint8_t *buffer,
                                            uint16_t buffer_size,
                                            uint16_t *actual_len,
                                            uint32_t wait_ms)
{
    bool ok;
    uint16_t actual;

    if(actual_len != NULL)
        *actual_len = 0U;
    if(socket_id < 0 || buffer == NULL || buffer_size == 0U || actual_len == NULL)
    {
        g_socket_recv_result = 4U;
        return false;
    }

    if(!app_w800_wait_socket_rx_data(socket_id, wait_ms))
    {
        g_socket_recv_result = 2U;
        g_socket_recv_actual = 0U;
        return true;
    }

    actual = 0U;
    ok = at_module_recv_socket_id(&g_module,
                                  socket_id,
                                  buffer,
                                  buffer_size,
                                  &actual);
    *actual_len = actual;
    g_socket_recv_actual = actual;
    g_socket_recv_head[0] = actual > 0U ? buffer[0] : 0U;
    g_socket_recv_head[1] = actual > 1U ? buffer[1] : 0U;
    g_socket_recv_head[2] = actual > 2U ? buffer[2] : 0U;
    g_socket_recv_head[3] = actual > 3U ? buffer[3] : 0U;
    if(ok)
    {
        g_socket_recv_result = 1U;
        return true;
    }

    g_socket_recv_result = 3U;
    g_socket_recv_fail_count++;
    return false;
}

static bool app_w800_open_socket_id(const char *host,
                                    uint16_t port,
                                    uint16_t local_port,
                                    int *socket_id)
{
    at_socket_config_t config;

    if(!host || port == 0U || !socket_id)
        return false;

    config.host = host;
    config.port = port;
    config.local_port = local_port;
    return at_module_open_socket_id(&g_module, &config, socket_id);
}

static bool app_w800_send_socket_id(int socket_id, const uint8_t *data, uint16_t len)
{
    uint16_t actual = 0U;

    if(socket_id < 0 || !data || len == 0U)
        return false;

    return at_module_send_socket_id(&g_module, socket_id, data, len, &actual) &&
           actual == len;
}

static void app_w800_clear_at_rx_state(void)
{
    at_session_clear_capture(&g_at_session);
    at_client_clear_result(&g_at_session.client);
}

static void app_w800_http_reset_stream_state(void)
{
    memset(&g_tcp_chunk_frame, 0, sizeof(g_tcp_chunk_frame));
}

static void app_w800_close_socket_id(int socket_id)
{
    if(socket_id < 0)
        return;

    (void)at_module_close_socket_id(&g_module, socket_id);
}

static ULONG app_w800_ms_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks;

    if(milliseconds == 0U)
        return TX_NO_WAIT;

    ticks = ((uint64_t)milliseconds * (uint64_t)TX_TIMER_TICKS_PER_SECOND + 999ULL) / 1000ULL;
    if(ticks == 0ULL)
        ticks = 1ULL;
    if(ticks > (uint64_t)ULONG_MAX)
        ticks = (uint64_t)ULONG_MAX;

    return (ULONG)ticks;
}

static void app_w800_ldc_event(ldc_easy_t *queue, ldc_easy_event_t event, void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_EASY_EVT_PACKET)
    {
        (void)tx_semaphore_put(&g_rx_sem);
    }
}

static int app_w800_at_tx(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    if(!data || length == 0U)
        return -1;
    return bsp_uart_write(BSP_UART_W800_AT,
                          data,
                          length,
                          APP_W800_TX_TIMEOUT_MS) == (int)length ? 0 : -1;
}

static const uint8_t *app_w800_find_bytes(const uint8_t *data,
                                          uint16_t length,
                                          const char *needle)
{
    uint16_t needle_len;

    if(!data || !needle)
        return NULL;

    needle_len = (uint16_t)strlen(needle);
    if(needle_len == 0U || length < needle_len)
        return NULL;

    for(uint16_t i = 0U; i <= (uint16_t)(length - needle_len); i++)
    {
        if(memcmp(&data[i], needle, needle_len) == 0)
            return &data[i];
    }

    return NULL;
}

static bool app_w800_is_digit(uint8_t ch)
{
    return ch >= (uint8_t)'0' && ch <= (uint8_t)'9';
}

static bool app_w800_parse_u16_token(const uint8_t *data,
                                     uint16_t length,
                                     uint16_t *offset,
                                     uint16_t *value)
{
    uint32_t parsed = 0U;
    uint16_t pos;
    bool seen_digit = false;

    if(!data || !offset || !value || *offset >= length)
        return false;

    pos = *offset;
    while(pos < length && app_w800_is_digit(data[pos]))
    {
        seen_digit = true;
        parsed = (parsed * 10U) + (uint32_t)(data[pos] - (uint8_t)'0');
        if(parsed > 65535U)
            return false;
        pos++;
    }

    if(!seen_digit)
        return false;

    *offset = pos;
    *value = (uint16_t)parsed;
    return true;
}

static void app_w800_skip_socket_payload_delimiter(const uint8_t *data,
                                                   uint16_t length,
                                                   uint16_t *offset)
{
    uint16_t pos;

    if(!data || !offset)
        return;

    pos = *offset;
    while(pos < length)
    {
        uint8_t ch = data[pos];
        if(ch != (uint8_t)',' && ch != (uint8_t)':' &&
           ch != (uint8_t)'\r' && ch != (uint8_t)'\n' &&
           ch != (uint8_t)' ')
        {
            break;
        }
        pos++;
    }

    *offset = pos;
}

static bool app_w800_json_get_string(const char *text,
                                     const char *key,
                                     char *out,
                                     uint16_t out_size)
{
    const char *pos;
    const char *colon;
    const char *start;
    const char *end;
    char pattern[32];
    uint16_t len;

    if(!text || !key || !out || out_size == 0U)
        return false;

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(text, pattern);
    if(!pos)
        return false;
    colon = strchr(pos, ':');
    if(!colon)
        return false;
    start = strchr(colon, '"');
    if(!start)
        return false;
    start++;
    end = strchr(start, '"');
    if(!end)
        return false;

    len = (uint16_t)(end - start);
    if(len >= out_size)
        len = out_size - 1U;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool app_w800_json_get_hex(
    const char *text,
    const char *key,
    uint8_t *out,
    uint16_t out_size)
{
    char hex[129];
    uint16_t index;

    if(out == NULL || out_size == 0U || out_size > (sizeof(hex) - 1U) / 2U ||
       !app_w800_json_get_string(text, key, hex, sizeof(hex)) ||
       strlen(hex) != (size_t)out_size * 2U)
    {
        return false;
    }

    for(index = 0U; index < out_size; index++)
    {
        uint8_t value = 0U;
        uint8_t nibble;
        uint8_t half;
        for(half = 0U; half < 2U; half++)
        {
            char c = hex[index * 2U + half];
            if(c >= '0' && c <= '9')
                nibble = (uint8_t)(c - '0');
            else if(c >= 'A' && c <= 'F')
                nibble = (uint8_t)(c - 'A' + 10);
            else if(c >= 'a' && c <= 'f')
                nibble = (uint8_t)(c - 'a' + 10);
            else
                return false;
            value = (uint8_t)((value << 4U) | nibble);
        }
        out[index] = value;
    }
    return true;
}

static bool app_w800_json_get_u32(const char *text, const char *key, uint32_t *out)
{
    const char *pos;
    const char *colon;
    char pattern[32];

    if(!text || !key || !out)
        return false;

    (void)snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(text, pattern);
    if(!pos)
        return false;
    colon = strchr(pos, ':');
    if(!colon)
        return false;
    *out = (uint32_t)strtoul(colon + 1, NULL, 0);
    return true;
}

static void app_w800_http_process_body(const uint8_t *data, uint16_t length)
{
    uint32_t remain;
    uint32_t take;

    if(!data || length == 0U || g_http_update.state != APP_W800_HTTP_BODY)
        return;

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_MANIFEST)
    {
        remain = g_http_update.content_length - g_http_update.received;
        take = length < remain ? length : remain;
        if(take == 0U)
            return;
        if((uint32_t)g_http_manifest_len + take >= sizeof(g_http_manifest))
        {
            app_w800_http_set_error("manifest big");
            return;
        }
        memcpy(&g_http_manifest[g_http_manifest_len], data, take);
        g_http_manifest_len = (uint16_t)(g_http_manifest_len + take);
        g_http_manifest[g_http_manifest_len] = '\0';
        g_http_update.received += take;
        if(g_http_update.received >= g_http_update.content_length)
        {
            g_http_update.state = APP_W800_HTTP_DONE;
            g_http_update.active = 0U;
        }
        return;
    }

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_RANGE)
    {
        remain = g_http_update.content_length - g_http_update.range_received;
        take = length < remain ? length : remain;
        if(take == 0U)
            return;
        if(g_http_update.range_received + take > sizeof(g_http_range_buffer))
        {
            app_w800_http_set_error("range big");
            return;
        }
        memcpy(&g_http_range_buffer[g_http_update.range_received], data, take);
        g_http_update.range_calc_crc32 = app_w800_crc32_update(g_http_update.range_calc_crc32, data, take);
        g_http_update.range_received += take;
        if(g_http_update.range_received >= g_http_update.content_length)
        {
            if(g_http_update.range_crc32 != 0U &&
               g_http_update.range_calc_crc32 != g_http_update.range_crc32)
            {
                app_w800_http_set_error("blk crc");
                return;
            }
            if((g_http_update.target == APP_W800_HTTP_TARGET_FIRMWARE &&
                app_firmware_update_service_write(
                    g_http_update.range_offset,
                    g_http_range_buffer,
                    g_http_update.range_received) != OTA_FIRMWARE_UPDATE_OK) ||
               (g_http_update.target == APP_W800_HTTP_TARGET_UI &&
                !ui_asset_update_write(g_http_update.range_offset,
                                       g_http_range_buffer,
                                       g_http_update.range_received)))
            {
                app_w800_http_set_error("flash");
                return;
            }
            g_http_update.calc_crc32 = app_w800_crc32_update(g_http_update.calc_crc32,
                                                             g_http_range_buffer,
                                                             g_http_update.range_received);
            g_http_update.received += g_http_update.range_received;
            g_http_update.state = APP_W800_HTTP_DONE;
            g_http_update.active = 0U;
        }
        return;
    }

    remain = g_http_update.content_length - g_http_update.received;
    take = length < remain ? length : remain;
    if(take == 0U)
        return;

    if(!ui_asset_update_write(g_http_update.received, data, take))
    {
        app_w800_http_set_error("flash");
        return;
    }

    g_http_update.calc_crc32 = app_w800_crc32_update(g_http_update.calc_crc32, data, take);
    g_http_update.received += take;

    if(g_http_update.received >= g_http_update.content_length)
    {
        if(g_http_update.size != 0U && g_http_update.received != g_http_update.size)
        {
            app_w800_http_set_error("size");
            return;
        }
        if(g_http_update.crc32 != 0U && g_http_update.calc_crc32 != g_http_update.crc32)
        {
            app_w800_http_set_error("crc");
            return;
        }
        if(!ui_asset_update_commit())
        {
            app_w800_http_set_error("commit");
            return;
        }
        g_http_update.state = APP_W800_HTTP_DONE;
        g_http_update.active = 0U;
        g_publish_status_requested = 1U;
    }
}

static void app_w800_http_parse_header_done(uint16_t body_offset)
{
    const char *content_len;
    uint32_t length;
    bool partial;

    partial = strstr(g_http_update.header, "HTTP/1.1 206") != NULL ||
              strstr(g_http_update.header, "HTTP/1.0 206") != NULL;

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_RANGE)
    {
        if(!partial)
        {
            app_w800_http_set_error("http 206");
            return;
        }
    }
    else if(strstr(g_http_update.header, "HTTP/1.1 200") == NULL &&
            strstr(g_http_update.header, "HTTP/1.0 200") == NULL)
    {
        app_w800_http_set_error("http status");
        return;
    }

    content_len = strstr(g_http_update.header, "Content-Length:");
    if(content_len == NULL)
        content_len = strstr(g_http_update.header, "content-length:");
    if(content_len == NULL)
    {
        app_w800_http_set_error("no length");
        return;
    }

    length = (uint32_t)strtoul(content_len + strlen("Content-Length:"), NULL, 0);
    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_MANIFEST)
    {
        if(length == 0U || length >= sizeof(g_http_manifest))
        {
            app_w800_http_set_error("bad manifest");
            return;
        }
        g_http_update.content_length = length;
        g_http_update.received = 0U;
        g_http_manifest_len = 0U;
        g_http_manifest[0] = '\0';
        g_http_update.state = APP_W800_HTTP_BODY;
        if(body_offset < g_http_update.header_len)
        {
            app_w800_http_process_body((const uint8_t *)&g_http_update.header[body_offset],
                                       (uint16_t)(g_http_update.header_len - body_offset));
        }
        return;
    }

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_RANGE)
    {
        const char *range_crc;

        if(length == 0U || length != g_http_update.range_length || length > sizeof(g_http_range_buffer))
        {
            app_w800_http_set_error("range length");
            return;
        }
        range_crc = strstr(g_http_update.header, "X-Range-CRC32:");
        if(range_crc == NULL)
            range_crc = strstr(g_http_update.header, "x-range-crc32:");
        if(range_crc == NULL)
        {
            app_w800_http_set_error("no blk crc");
            return;
        }
        g_http_update.content_length = length;
        g_http_update.range_received = 0U;
        g_http_update.range_crc32 = (uint32_t)strtoul(range_crc + strlen("X-Range-CRC32:"), NULL, 0);
        g_http_update.range_calc_crc32 = 0U;
        g_http_update.state = APP_W800_HTTP_BODY;
        if(body_offset < g_http_update.header_len)
        {
            app_w800_http_process_body((const uint8_t *)&g_http_update.header[body_offset],
                                       (uint16_t)(g_http_update.header_len - body_offset));
        }
        return;
    }

    if(length == 0U || length > UI_ASSET_SLOT_SIZE ||
       (g_http_update.size != 0U && length != g_http_update.size))
    {
        app_w800_http_set_error("bad length");
        return;
    }

    g_http_update.content_length = length;
    g_http_update.received = 0U;
    g_http_update.calc_crc32 = 0U;

    if(!ui_asset_update_begin(length, g_http_update.version))
    {
        app_w800_http_set_error("begin");
        return;
    }

    g_http_update.state = APP_W800_HTTP_BODY;

    if(body_offset < g_http_update.header_len)
    {
        app_w800_http_process_body((const uint8_t *)&g_http_update.header[body_offset],
                                   (uint16_t)(g_http_update.header_len - body_offset));
    }
}

static void app_w800_http_stream_input(const uint8_t *data, uint16_t length)
{
    const uint8_t *start;

    if(!data || length == 0U || g_http_update.active == 0U)
        return;

    if(g_http_update.state == APP_W800_HTTP_HEADER)
    {
        start = app_w800_find_bytes(data, length, "HTTP/1.");
        if(start != NULL)
        {
            uint16_t skip = (uint16_t)(start - data);
            data += skip;
            length = (uint16_t)(length - skip);
        }
        else if(g_http_update.header_len == 0U)
        {
            return;
        }

        while(length != 0U && g_http_update.state == APP_W800_HTTP_HEADER)
        {
            if(g_http_update.header_len >= (sizeof(g_http_update.header) - 1U))
            {
                app_w800_http_set_error("header big");
                return;
            }

            g_http_update.header[g_http_update.header_len++] = (char)*data++;
            g_http_debug_header_len = g_http_update.header_len;
            length--;
            g_http_update.header[g_http_update.header_len] = '\0';

            if(g_http_update.header_len >= 4U &&
               memcmp(&g_http_update.header[g_http_update.header_len - 4U], "\r\n\r\n", 4U) == 0)
            {
                app_w800_http_parse_header_done(g_http_update.header_len);
            }
        }
    }

    if(length != 0U && g_http_update.state == APP_W800_HTTP_BODY)
        app_w800_http_process_body(data, length);
}

static bool app_w800_tcp_chunk_parse_header(void)
{
    unsigned long version;
    unsigned long offset;
    unsigned long length;
    unsigned long seq;
    unsigned long crc32;

    if(sscanf(g_tcp_chunk_frame.header,
              "UIBLK %lu %lu %lu %lu %lu",
              &version,
              &offset,
              &length,
              &seq,
              &crc32) != 5)
    {
        app_w800_http_set_error("blk header");
        return false;
    }

    if((uint32_t)version != g_http_update.version ||
       (uint32_t)offset != g_tcp_chunk_frame.offset ||
       (uint32_t)seq != g_tcp_chunk_frame.seq ||
       length == 0UL ||
       length > g_tcp_chunk_frame.length ||
       length > APP_W800_TCP_CHUNK_DEFAULT)
    {
        app_w800_http_set_error("blk meta");
        return false;
    }

    g_tcp_chunk_frame.crc32 = (uint32_t)crc32;
    g_tcp_chunk_frame.remaining = (uint16_t)length;
    g_tcp_chunk_frame.calc_crc32 = 0U;
    g_tcp_chunk_frame.header_done = 1U;
    return true;
}

static void app_w800_tcp_chunk_stream_input(const uint8_t *data, uint16_t length)
{
    uint16_t offset = 0U;

    if(!data || length == 0U || g_tcp_chunk_frame.waiting == 0U)
        return;

    while(offset < length && g_tcp_chunk_frame.waiting != 0U &&
          g_http_update.state != APP_W800_HTTP_ERROR)
    {
        if(g_tcp_chunk_frame.header_done == 0U)
        {
            if(g_tcp_chunk_frame.header_len == 0U)
            {
                const uint8_t *header = app_w800_find_bytes(&data[offset],
                                                            (uint16_t)(length - offset),
                                                            "UIBLK");
                if(header == NULL)
                    return;
                offset = (uint16_t)(header - data);
            }

            while(offset < length && g_tcp_chunk_frame.header_done == 0U)
            {
                char ch = (char)data[offset++];

                if(ch == '\r')
                    continue;
                if(ch == '\n')
                {
                    g_tcp_chunk_frame.header[g_tcp_chunk_frame.header_len] = '\0';
                    if(!app_w800_tcp_chunk_parse_header())
                        return;
                    break;
                }
                if(g_tcp_chunk_frame.header_len >= (sizeof(g_tcp_chunk_frame.header) - 1U))
                {
                    app_w800_http_set_error("blk hdr big");
                    return;
                }
                g_tcp_chunk_frame.header[g_tcp_chunk_frame.header_len++] = ch;
            }
        }

        if(g_tcp_chunk_frame.header_done != 0U && offset < length)
        {
            uint16_t take = (uint16_t)(length - offset);
            uint32_t write_offset;

            if(take > g_tcp_chunk_frame.remaining)
                take = g_tcp_chunk_frame.remaining;

            write_offset = g_tcp_chunk_frame.offset +
                           (g_tcp_chunk_frame.length - g_tcp_chunk_frame.remaining);
            if(!ui_asset_update_write(write_offset, &data[offset], take))
            {
                app_w800_http_set_error("flash");
                return;
            }

            g_tcp_chunk_frame.calc_crc32 = app_w800_crc32_update(g_tcp_chunk_frame.calc_crc32,
                                                                 &data[offset],
                                                                 take);
            g_http_update.calc_crc32 = app_w800_crc32_update(g_http_update.calc_crc32,
                                                             &data[offset],
                                                             take);
            g_http_update.received += take;
            g_tcp_chunk_frame.remaining = (uint16_t)(g_tcp_chunk_frame.remaining - take);
            offset = (uint16_t)(offset + take);

            if(g_tcp_chunk_frame.remaining == 0U)
            {
                if(g_tcp_chunk_frame.calc_crc32 != g_tcp_chunk_frame.crc32)
                {
                    app_w800_http_set_error("blk crc");
                    return;
                }
                g_tcp_chunk_frame.waiting = 0U;
                g_tcp_chunk_frame.header_done = 0U;
                g_tcp_chunk_frame.header_len = 0U;
                g_tcp_chunk_frame.header[0] = '\0';
            }
        }
    }
}

static bool app_w800_chunk_start(uint32_t size,
                                 uint32_t version,
                                 uint32_t crc32,
                                 uint32_t chunk_size)
{
    if(size == 0U || size > UI_ASSET_SLOT_SIZE)
    {
        app_w800_chunk_set_error("bad size");
        return false;
    }

    if(chunk_size == 0U || chunk_size > APP_W800_UI_CHUNK_MAX)
        chunk_size = APP_W800_UI_CHUNK_DEFAULT;

    g_http_update.active = 0U;
    app_w800_http_reset_stream_state();

    memset(&g_chunk_update, 0, sizeof(g_chunk_update));
    g_chunk_update.size = size;
    g_chunk_update.version = version;
    g_chunk_update.crc32 = crc32;
    g_chunk_update.chunk_size = (uint16_t)chunk_size;
    g_chunk_update.state = APP_W800_CHUNK_REQUEST;
    g_chunk_update.active = 1U;
    g_chunk_update.last_progress_ms = app_w800_now_ms(NULL);
    g_chunk_update.error[0] = '\0';

    if(!ui_asset_update_begin(size, version))
    {
        app_w800_chunk_set_error("begin");
        return false;
    }

    g_ui_status_next_report = app_w800_now_ms(NULL);
    g_publish_status_requested = 0U;
    g_publish_config_requested = 0U;
    g_publish_data_requested = 0U;
    return true;
}

static void app_w800_apply_json_command(const char *text)
{
    char cmd[24];
    char value[24];
    char key[24];
    char mbctl[128];
    uint32_t port;
    uint32_t slave;
    uint32_t fc;
    uint32_t addr;
    uint32_t count;
    uint32_t period;
    uint32_t size;
    uint32_t version;
    uint32_t crc32;
    uint32_t chunk;
    uint32_t enabled;
    uint32_t idle_ms;
    uint32_t max_seconds;
    uint32_t sources;
    char mark[BLACKBOX_STORE_PAYLOAD_SIZE + 1U];

    if(!text || !app_w800_json_get_string(text, "cmd", cmd, sizeof(cmd)))
        return;

    if(g_chunk_update.active != 0U && strcmp(cmd, "ui_update_cancel") != 0)
        return;

    if(strcmp(cmd, "status") == 0)
    {
        g_publish_status_requested = 1U;
        return;
    }

    if(strcmp(cmd, "diagnostics") == 0)
    {
        g_publish_ops_requested = APP_W800_OPS_DIAGNOSTICS;
        return;
    }

    if(strcmp(cmd, "self_test_run") == 0)
    {
        app_self_test_snapshot_t self_test;

        app_self_test_get_snapshot(&self_test);
        g_remote_self_test_generation = self_test.generation;
        app_self_test_request_run();
        g_remote_self_test_watch = 1U;
        g_publish_ops_requested = APP_W800_OPS_DIAGNOSTICS;
        return;
    }

    if(strcmp(cmd, "production_run") == 0)
    {
        if(app_production_test_start(app_w800_now_ms(NULL)))
        {
            g_remote_production_watch = 1U;
            g_publish_ops_requested = APP_W800_OPS_PRODUCTION;
        }
        return;
    }

    if(strcmp(cmd, "blackbox_status") == 0)
    {
        g_publish_ops_requested = APP_W800_OPS_BLACKBOX;
        return;
    }

    if(strcmp(cmd, "blackbox_mark") == 0)
    {
        if(app_w800_json_get_string(text, "text", mark, sizeof(mark)))
        {
            (void)app_blackbox_record(APP_BLACKBOX_EVENT_MANUAL,
                                      APP_BLACKBOX_SEVERITY_INFO,
                                      0x800U,
                                      0x0001U,
                                      mark,
                                      (uint16_t)strlen(mark));
            g_publish_ops_requested = APP_W800_OPS_BLACKBOX;
        }
        return;
    }

    if(strcmp(cmd, "can_bus_off") == 0)
    {
        if(app_w800_json_get_u32(text, "target", &port) &&
           app_can_self_test_request_bus_off((uint8_t)port) == BSP_STATUS_OK)
        {
            g_publish_ops_requested = APP_W800_OPS_DIAGNOSTICS;
        }
        return;
    }

    if(strcmp(cmd, "power_stop") == 0)
    {
        sources = APP_POWER_WAKE_SOURCE_RTC;
        if(app_w800_json_get_u32(text, "seconds", &period))
        {
            (void)app_w800_json_get_u32(text, "sources", &sources);
            if(app_power_request_stop(period, sources) == 0)
                g_publish_ops_requested = APP_W800_OPS_DIAGNOSTICS;
        }
        return;
    }

    if(strcmp(cmd, "power_auto") == 0)
    {
        app_power_snapshot_t power;

        enabled = 0U;
        app_power_get_snapshot(&power);
        idle_ms = power.auto_idle_ms;
        max_seconds = power.auto_max_sleep_seconds;
        sources = APP_POWER_WAKE_SOURCE_RTC;
        if(app_w800_json_get_u32(text, "enabled", &enabled))
        {
            (void)app_w800_json_get_u32(text, "idle_ms", &idle_ms);
            (void)app_w800_json_get_u32(text, "max_seconds", &max_seconds);
            (void)app_w800_json_get_u32(text, "sources", &sources);
            if(app_power_configure_auto((uint8_t)enabled,
                                        idle_ms,
                                        max_seconds,
                                        sources) == 0)
            {
                g_publish_ops_requested = APP_W800_OPS_DIAGNOSTICS;
            }
        }
        return;
    }

    if(strcmp(cmd, "get_config") == 0)
    {
        g_publish_config_requested = 1U;
        return;
    }

    if(strcmp(cmd, "save_config") == 0)
    {
        g_publish_config_requested = 1U;
        return;
    }

    if(strcmp(cmd, "ui_http_update") == 0)
    {
        if(app_w800_json_get_string(text, "host", g_http_update.host, sizeof(g_http_update.host)) &&
           app_w800_json_get_string(text, "path", g_http_update.path, sizeof(g_http_update.path)) &&
           app_w800_json_get_u32(text, "port", &port) &&
           app_w800_json_get_u32(text, "size", &size) &&
           app_w800_json_get_u32(text, "version", &version))
        {
            crc32 = 0U;
            (void)app_w800_json_get_u32(text, "crc32", &crc32);
            g_http_update.port = (uint16_t)port;
            g_http_update.size = size;
            g_http_update.version = version;
            g_http_update.crc32 = crc32;
            g_http_update.pending = 1U;
            g_http_update.raw_tcp = APP_W800_HTTP_MODE_FULL;
            g_http_update.target = APP_W800_HTTP_TARGET_UI;
            g_http_update.error[0] = '\0';
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(strcmp(cmd, "ui_http_manifest_update") == 0)
    {
        if(app_w800_json_get_string(text, "host", g_http_update.host, sizeof(g_http_update.host)) &&
           app_w800_json_get_string(text, "manifest", g_http_update.path, sizeof(g_http_update.path)) &&
           app_w800_json_get_u32(text, "port", &port))
        {
            uint32_t chunk_size = APP_W800_HTTP_RANGE_DEFAULT;

            g_http_update.port = (uint16_t)port;
            g_http_update.target = APP_W800_HTTP_TARGET_UI;
            g_http_update.size = 0U;
            g_http_update.version = 0U;
            g_http_update.crc32 = 0U;
            if(app_w800_json_get_u32(text, "size", &size) &&
               app_w800_json_get_u32(text, "version", &version) &&
               app_w800_json_get_u32(text, "crc32", &crc32))
            {
                (void)app_w800_json_get_string(text,
                                               "assetPath",
                                               g_http_update.path,
                                               sizeof(g_http_update.path));
                if(g_http_update.path[0] == '\0' || strcmp(g_http_update.path, "/ui/manifest.json") == 0)
                    (void)strncpy(g_http_update.path, "/ui/ui_assets.bin", sizeof(g_http_update.path) - 1U);
                (void)app_w800_json_get_u32(text, "chunkSize", &chunk_size);
                if(chunk_size > APP_W800_HTTP_RANGE_MAX)
                    chunk_size = APP_W800_HTTP_RANGE_MAX;
                if(chunk_size < APP_W800_HTTP_RANGE_MIN)
                    chunk_size = APP_W800_HTTP_RANGE_DEFAULT;
                g_http_update.size = size;
                g_http_update.version = version;
                g_http_update.crc32 = crc32;
                g_http_direct_chunk_size = chunk_size;
                g_http_update.raw_tcp = APP_W800_HTTP_MODE_RANGE;
            }
            else
            {
                g_http_update.raw_tcp = APP_W800_HTTP_MODE_MANIFEST;
            }
            g_http_update.pending = 1U;
            g_http_update.error[0] = '\0';
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(strcmp(cmd, "fw_http_update") == 0)
    {
        uint32_t entry_address;
        uint32_t image_flags;
        uint32_t chunk_size = APP_W800_HTTP_RANGE_DEFAULT;

        if(app_w800_json_get_string(text, "host", g_http_update.host, sizeof(g_http_update.host)) &&
           app_w800_json_get_string(text, "path", g_http_update.path, sizeof(g_http_update.path)) &&
           app_w800_json_get_u32(text, "port", &port) &&
           app_w800_json_get_u32(text, "size", &size) &&
           app_w800_json_get_u32(text, "version", &version) &&
           app_w800_json_get_u32(text, "crc32", &crc32) &&
           app_w800_json_get_u32(text, "imageFlags", &image_flags) &&
           app_w800_json_get_u32(text, "entryAddress", &entry_address) &&
           app_w800_json_get_hex(text, "sha256", g_http_update.image_sha256,
                                  sizeof(g_http_update.image_sha256)) &&
           app_w800_json_get_hex(text, "signature", g_http_update.signature,
                                  sizeof(g_http_update.signature)))
        {
            (void)app_w800_json_get_u32(text, "chunkSize", &chunk_size);
            if(chunk_size > APP_W800_HTTP_RANGE_MAX)
                chunk_size = APP_W800_HTTP_RANGE_MAX;
            if(chunk_size < APP_W800_HTTP_RANGE_MIN)
                chunk_size = APP_W800_HTTP_RANGE_DEFAULT;

            g_http_update.port = (uint16_t)port;
            g_http_update.size = size;
            g_http_update.version = version;
            g_http_update.crc32 = crc32;
            g_http_update.image_flags = image_flags;
            g_http_update.entry_address = entry_address;
            g_http_update.target = APP_W800_HTTP_TARGET_FIRMWARE;
            g_http_update.raw_tcp = APP_W800_HTTP_MODE_RANGE;
            g_http_direct_chunk_size = chunk_size;
            g_http_update.pending = 1U;
            g_http_update.error[0] = '\0';
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(strcmp(cmd, "ui_tcp_update") == 0)
    {
        if(app_w800_json_get_string(text, "host", g_http_update.host, sizeof(g_http_update.host)) &&
           app_w800_json_get_u32(text, "port", &port) &&
           app_w800_json_get_u32(text, "size", &size) &&
           app_w800_json_get_u32(text, "version", &version))
        {
            crc32 = 0U;
            (void)app_w800_json_get_u32(text, "crc32", &crc32);
            g_http_update.path[0] = '\0';
            g_http_update.port = (uint16_t)port;
            g_http_update.size = size;
            g_http_update.version = version;
            g_http_update.crc32 = crc32;
            g_http_update.pending = 1U;
            g_http_update.raw_tcp = APP_W800_HTTP_MODE_RAW;
            g_http_update.error[0] = '\0';
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(strcmp(cmd, "ui_tcp_chunk_update") == 0)
    {
        if(app_w800_json_get_string(text, "host", g_http_update.host, sizeof(g_http_update.host)) &&
           app_w800_json_get_u32(text, "port", &port) &&
           app_w800_json_get_u32(text, "size", &size) &&
           app_w800_json_get_u32(text, "version", &version))
        {
            crc32 = 0U;
            (void)app_w800_json_get_u32(text, "crc32", &crc32);
            g_http_update.path[0] = '\0';
            g_http_update.port = (uint16_t)port;
            g_http_update.size = size;
            g_http_update.version = version;
            g_http_update.crc32 = crc32;
            g_http_update.pending = 1U;
            g_http_update.raw_tcp = APP_W800_HTTP_MODE_TCP_CHUNK;
            g_http_update.error[0] = '\0';
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(strcmp(cmd, "ui_chunk_update") == 0)
    {
        if(app_w800_json_get_u32(text, "size", &size) &&
           app_w800_json_get_u32(text, "version", &version))
        {
            crc32 = 0U;
            chunk = APP_W800_UI_CHUNK_DEFAULT;
            (void)app_w800_json_get_u32(text, "crc32", &crc32);
            (void)app_w800_json_get_u32(text, "chunk", &chunk);
            (void)app_w800_chunk_start(size, version, crc32, chunk);
        }
        return;
    }

    if(strcmp(cmd, "ui_update_cancel") == 0)
    {
        if(g_chunk_update.active != 0U)
            app_w800_chunk_set_error("cancel");
        g_http_update.pending = 0U;
        g_http_update.active = 0U;
        g_publish_status_requested = 1U;
        return;
    }

    if(strcmp(cmd, "set_ports") == 0)
    {
        if(app_w800_json_get_string(text, "ports", value, sizeof(value)))
        {
            (void)snprintf(mbctl, sizeof(mbctl), "mbctl:ports=%s", value);
            if(app_rs485_apply_network_command(mbctl))
            {
                g_publish_config_requested = 1U;
                g_publish_status_requested = 1U;
            }
        }
        return;
    }

    if(strcmp(cmd, "set_config") == 0)
    {
        if(app_w800_json_get_string(text, "ports", value, sizeof(value)))
        {
            (void)snprintf(mbctl, sizeof(mbctl), "mbctl:ports=%s", value);
            (void)app_rs485_apply_network_command(mbctl);
        }

        for(uint32_t p = 2U; p <= 4U; p += 2U)
        {
            (void)snprintf(key, sizeof(key), "p%lu_slave", (unsigned long)p);
            if(!app_w800_json_get_u32(text, key, &slave))
                continue;
            (void)snprintf(key, sizeof(key), "p%lu_fc", (unsigned long)p);
            if(!app_w800_json_get_u32(text, key, &fc))
                continue;
            (void)snprintf(key, sizeof(key), "p%lu_addr", (unsigned long)p);
            if(!app_w800_json_get_u32(text, key, &addr))
                continue;
            (void)snprintf(key, sizeof(key), "p%lu_count", (unsigned long)p);
            if(!app_w800_json_get_u32(text, key, &count))
                continue;
            (void)snprintf(key, sizeof(key), "p%lu_period", (unsigned long)p);
            if(!app_w800_json_get_u32(text, key, &period))
                continue;

            (void)snprintf(mbctl,
                           sizeof(mbctl),
                           "mbctl:poll=%lu,%lu,%lu,%lu,%lu,%lu",
                           (unsigned long)p,
                           (unsigned long)slave,
                           (unsigned long)fc,
                           (unsigned long)addr,
                           (unsigned long)count,
                           (unsigned long)period);
            (void)app_rs485_apply_network_command(mbctl);
        }

        g_publish_config_requested = 1U;
        g_publish_status_requested = 1U;
        g_publish_data_requested = 1U;
        return;
    }

    if(strcmp(cmd, "set_poll") == 0)
    {
        if(app_w800_json_get_u32(text, "port", &port) &&
           app_w800_json_get_u32(text, "slave", &slave) &&
           app_w800_json_get_u32(text, "fc", &fc) &&
           app_w800_json_get_u32(text, "addr", &addr) &&
           app_w800_json_get_u32(text, "count", &count) &&
           app_w800_json_get_u32(text, "period", &period))
        {
            (void)snprintf(mbctl,
                           sizeof(mbctl),
                           "mbctl:poll=%lu,%lu,%lu,%lu,%lu,%lu",
                           (unsigned long)port,
                           (unsigned long)slave,
                           (unsigned long)fc,
                           (unsigned long)addr,
                           (unsigned long)count,
                           (unsigned long)period);
            if(app_rs485_apply_network_command(mbctl))
            {
                g_publish_config_requested = 1U;
                g_publish_data_requested = 1U;
            }
        }
    }
}

static bool app_w800_topic_equals(const uint8_t *topic, uint16_t topic_len, const char *expected)
{
    uint16_t expected_len;

    if(!topic || !expected)
        return false;

    expected_len = (uint16_t)strlen(expected);
    return topic_len == expected_len && memcmp(topic, expected, expected_len) == 0;
}

static bool app_w800_ui_update_in_progress(void)
{
    uint32_t expected = ui_asset_update_expected();

    return expected != 0U && ui_asset_update_received() < expected;
}

static void app_w800_report_ui_update_progress(void)
{
    uint32_t now = app_w800_now_ms(NULL);

    if(g_chunk_update.active != 0U)
        return;

    if((uint32_t)(now - g_ui_status_next_report) >= 2000U)
    {
        g_publish_status_requested = 1U;
        g_ui_status_next_report = now;
    }
}

static uint32_t app_w800_chunk_next_len(void)
{
    uint32_t remain;

    if(g_chunk_update.received >= g_chunk_update.size)
        return 0U;

    remain = g_chunk_update.size - g_chunk_update.received;
    return remain > g_chunk_update.chunk_size ? g_chunk_update.chunk_size : remain;
}

static bool app_w800_publish_ui_chunk_request(void)
{
    uint16_t packet_length;
    uint32_t request_len = app_w800_chunk_next_len();

    if(request_len == 0U)
        return false;

    g_chunk_update.seq++;
    (void)snprintf(g_mqtt_ui_req_payload,
                   sizeof(g_mqtt_ui_req_payload),
                   "{\"cmd\":\"ui_chunk_req\",\"version\":%lu,\"offset\":%lu,\"len\":%lu,\"seq\":%u}",
                   (unsigned long)g_chunk_update.version,
                   (unsigned long)g_chunk_update.received,
                   (unsigned long)request_len,
                   (unsigned int)g_chunk_update.seq);

    packet_length = mqtt_build_publish(g_mqtt_ui_req_packet,
                                       sizeof(g_mqtt_ui_req_packet),
                                       APP_W800_MQTT_UI_REQ_TOPIC,
                                       g_mqtt_ui_req_payload);
    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_ui_req_packet, packet_length);
}

static void app_w800_ui_chunk_step(uint32_t now)
{
    if(g_chunk_update.active == 0U)
        return;

    if(g_chunk_update.state == APP_W800_CHUNK_REQUEST)
    {
        if(g_chunk_update.retry > APP_W800_UI_CHUNK_MAX_RETRY)
        {
            app_w800_chunk_set_error("retry");
            return;
        }

        g_chunk_update.last_request_ms = now;
        g_chunk_update.state = APP_W800_CHUNK_WAIT;
        if(!app_w800_publish_ui_chunk_request())
        {
            g_chunk_update.state = APP_W800_CHUNK_REQUEST;
            g_chunk_update.retry++;
        }
        return;
    }

    if(g_chunk_update.state == APP_W800_CHUNK_WAIT &&
       (uint32_t)(now - g_chunk_update.last_request_ms) >= APP_W800_UI_CHUNK_TIMEOUT_MS)
    {
        g_chunk_update.retry++;
        g_chunk_update.state = APP_W800_CHUNK_REQUEST;
    }
}

static void app_w800_handle_ui_chunk_json(const uint8_t *payload, uint16_t payload_len)
{
    uint32_t version;
    uint32_t offset;
    uint32_t length;
    uint32_t crc32;
    uint32_t seq;
    uint16_t decoded_len;
    uint32_t calc_crc;

    if(g_chunk_update.active != 0U)
        g_chunk_update.json_seen++;

    if(payload_len == 0U || payload_len >= sizeof(g_network_command))
    {
        if(g_chunk_update.active != 0U)
            g_chunk_update.json_drop++;
        return;
    }

    memcpy(g_network_command, payload, payload_len);
    g_network_command[payload_len] = '\0';

    if(g_chunk_update.active == 0U ||
       g_chunk_update.state != APP_W800_CHUNK_WAIT ||
       !app_w800_json_get_u32(g_network_command, "version", &version) ||
       !app_w800_json_get_u32(g_network_command, "offset", &offset) ||
       !app_w800_json_get_u32(g_network_command, "len", &length) ||
       !app_w800_json_get_u32(g_network_command, "crc32", &crc32) ||
       !app_w800_json_get_string(g_network_command, "data", g_chunk_base64, sizeof(g_chunk_base64)))
    {
        if(g_chunk_update.active != 0U)
            g_chunk_update.json_drop++;
        return;
    }

    seq = 0U;
    (void)app_w800_json_get_u32(g_network_command, "seq", &seq);
    if(seq != 0U && seq != g_chunk_update.seq)
    {
        g_chunk_update.seq_error++;
        return;
    }

    if(version != g_chunk_update.version || offset != g_chunk_update.received)
    {
        g_chunk_update.offset_error++;
        return;
    }

    if(length == 0U ||
       length > APP_W800_UI_CHUNK_MAX ||
       offset > g_chunk_update.size ||
       length > (g_chunk_update.size - offset))
    {
        app_w800_chunk_set_error("bad chunk");
        return;
    }

    if(!app_w800_base64_decode(g_chunk_base64, g_chunk_data, sizeof(g_chunk_data), &decoded_len) ||
       decoded_len != (uint16_t)length)
    {
        g_chunk_update.b64_error++;
        g_chunk_update.retry++;
        g_chunk_update.state = APP_W800_CHUNK_REQUEST;
        return;
    }

    calc_crc = app_w800_crc32_update(0U, g_chunk_data, decoded_len);
    if(calc_crc != crc32)
    {
        g_chunk_update.crc_error++;
        g_chunk_update.retry++;
        g_chunk_update.state = APP_W800_CHUNK_REQUEST;
        return;
    }

    if(!ui_asset_update_write(offset, g_chunk_data, decoded_len))
    {
        app_w800_chunk_set_error("flash");
        return;
    }

    g_chunk_update.calc_crc32 = app_w800_crc32_update(g_chunk_update.calc_crc32,
                                                      g_chunk_data,
                                                      decoded_len);
    g_chunk_update.received += decoded_len;
    g_chunk_update.retry = 0U;
    g_chunk_update.last_progress_ms = app_w800_now_ms(NULL);

    if(g_chunk_update.received >= g_chunk_update.size)
    {
        if(g_chunk_update.crc32 != 0U && g_chunk_update.calc_crc32 != g_chunk_update.crc32)
        {
            app_w800_chunk_set_error("pkg crc");
            return;
        }
        if(!ui_asset_update_commit())
        {
            app_w800_chunk_set_error("commit");
            return;
        }

        g_chunk_update.state = APP_W800_CHUNK_DONE;
        g_chunk_update.active = 0U;
        g_publish_status_requested = 1U;
        return;
    }

    g_chunk_update.state = APP_W800_CHUNK_REQUEST;
    if((g_chunk_update.received & (OTA_EXT_SECTOR_SIZE - 1U)) == 0U)
        g_publish_status_requested = 1U;
    else
        app_w800_report_ui_update_progress();
}

static void app_w800_handle_mqtt_publish(const uint8_t *packet, uint16_t length, uint16_t fixed_header_len)
{
    const uint8_t *topic;
    const uint8_t *payload;
    uint16_t topic_len;
    uint16_t payload_len;
    uint16_t packet_id = 0U;
    uint16_t pos;
    uint8_t qos;

    if(!packet || length < (fixed_header_len + 2U))
    {
        g_mqtt_last_publish_reason = 1U;
        return;
    }

    qos = (uint8_t)((packet[0] >> 1) & 0x03U);
    if(qos == 3U)
    {
        g_mqtt_last_publish_reason = 2U;
        return;
    }

    pos = fixed_header_len;
    topic_len = (uint16_t)(((uint16_t)packet[pos] << 8) | packet[pos + 1U]);
    g_mqtt_last_topic_len = topic_len;
    pos += 2U;
    if(topic_len == 0U || (uint32_t)pos + topic_len > length)
    {
        g_mqtt_last_publish_reason = 3U;
        return;
    }

    topic = &packet[pos];
    pos = (uint16_t)(pos + topic_len);
    if(qos != 0U)
    {
        if((uint32_t)pos + 2U > length)
        {
            g_mqtt_last_publish_reason = 4U;
            return;
        }
        packet_id = (uint16_t)(((uint16_t)packet[pos] << 8) | packet[pos + 1U]);
        pos = (uint16_t)(pos + 2U);
    }

    payload = &packet[pos];
    payload_len = (uint16_t)(length - pos);
    g_mqtt_publish_seen++;
    g_mqtt_last_publish_reason = 5U;
    g_mqtt_last_payload_len = payload_len;
    if(qos == 1U)
        (void)app_w800_mqtt_puback(packet_id);
    {
        uint16_t copy_len = topic_len;
        if(copy_len >= sizeof(g_mqtt_last_topic))
            copy_len = sizeof(g_mqtt_last_topic) - 1U;
        memcpy(g_mqtt_last_topic, topic, copy_len);
        g_mqtt_last_topic[copy_len] = '\0';
    }

    if(app_w800_topic_equals(topic, topic_len, APP_W800_MQTT_UI_BEGIN_TOPIC))
    {
        g_mqtt_begin_seen++;
        if(payload_len >= 8U &&
           ui_asset_update_begin(app_w800_get_u32_le(payload),
                                 app_w800_get_u32_le(&payload[4])))
        {
            g_publish_status_requested = 1U;
            g_ui_status_next_report = app_w800_now_ms(NULL);
        }
        else
        {
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(app_w800_topic_equals(topic, topic_len, APP_W800_MQTT_UI_CHUNK_TOPIC))
    {
        g_mqtt_chunk_seen++;
        if(payload_len != 0U && payload[0] == (uint8_t)'{')
        {
            app_w800_handle_ui_chunk_json(payload, payload_len);
            return;
        }
        if(payload_len > 4U &&
           g_chunk_update.active == 0U &&
           ui_asset_update_write(app_w800_get_u32_le(payload),
                                 &payload[4],
                                 (uint32_t)payload_len - 4U))
        {
            if((ui_asset_update_received() & (OTA_EXT_SECTOR_SIZE - 1U)) == 0U)
                g_publish_status_requested = 1U;
            else
                app_w800_report_ui_update_progress();
        }
        else
        {
            g_publish_status_requested = 1U;
        }
        return;
    }

    if(app_w800_topic_equals(topic, topic_len, APP_W800_MQTT_UI_COMMIT_TOPIC))
    {
        g_mqtt_commit_seen++;
        (void)ui_asset_update_commit();
        g_publish_status_requested = 1U;
        return;
    }

    if(app_w800_topic_equals(topic, topic_len, APP_W800_MQTT_CMD_TOPIC))
    {
        uint16_t copy_len = payload_len;

        if(copy_len >= sizeof(g_network_command))
            copy_len = sizeof(g_network_command) - 1U;
        memcpy(g_network_command, payload, copy_len);
        g_network_command[copy_len] = '\0';
        app_w800_apply_json_command(g_network_command);
        return;
    }
}

static void app_w800_process_mqtt_stream(void)
{
    while(g_mqtt_rx_len >= 2U)
    {
        uint32_t remaining = 0U;
        uint32_t multiplier = 1U;
        uint16_t pos = 1U;
        uint16_t packet_len;
        uint8_t encoded;
        bool bad_remaining_length = false;

        if((g_mqtt_rx_stream[0] & 0xF0U) == 0U)
        {
            memmove(g_mqtt_rx_stream, &g_mqtt_rx_stream[1], (size_t)g_mqtt_rx_len - 1U);
            g_mqtt_rx_len--;
            continue;
        }

        do
        {
            if(pos >= g_mqtt_rx_len)
                return;

            encoded = g_mqtt_rx_stream[pos++];
            remaining += (uint32_t)(encoded & 0x7FU) * multiplier;
            multiplier *= 128U;
            if(multiplier > (128UL * 128UL * 128UL * 128UL))
            {
                bad_remaining_length = true;
                break;
            }
        } while((encoded & 0x80U) != 0U);

        if(bad_remaining_length)
        {
            g_mqtt_stream_drops++;
            memmove(g_mqtt_rx_stream, &g_mqtt_rx_stream[1], (size_t)g_mqtt_rx_len - 1U);
            g_mqtt_rx_len--;
            continue;
        }

        if(remaining > (APP_W800_MQTT_RX_STREAM_SIZE - pos))
        {
            g_mqtt_stream_drops++;
            memmove(g_mqtt_rx_stream, &g_mqtt_rx_stream[1], (size_t)g_mqtt_rx_len - 1U);
            g_mqtt_rx_len--;
            continue;
        }

        packet_len = (uint16_t)(pos + remaining);
        if(g_mqtt_rx_len < packet_len)
            return;

        g_mqtt_packet_seen++;
        g_mqtt_last_packet_type = (uint8_t)(g_mqtt_rx_stream[0] >> 4);
        g_mqtt_last_packet_len = packet_len;
        g_mqtt_last_fixed_header_len = pos;
        g_mqtt_last_b0 = g_mqtt_rx_stream[0];
        g_mqtt_last_b1 = (packet_len > 1U) ? g_mqtt_rx_stream[1] : 0U;
        g_mqtt_last_b2 = (packet_len > 2U) ? g_mqtt_rx_stream[2] : 0U;
        g_mqtt_last_b3 = (packet_len > 3U) ? g_mqtt_rx_stream[3] : 0U;
        g_mqtt_last_b4 = (packet_len > 4U) ? g_mqtt_rx_stream[4] : 0U;
        g_mqtt_last_b5 = (packet_len > 5U) ? g_mqtt_rx_stream[5] : 0U;
        if(g_mqtt_last_packet_type == 3U)
        {
            app_w800_handle_mqtt_publish(g_mqtt_rx_stream, packet_len, pos);
            if(g_mqtt_last_publish_reason != 5U)
            {
                uint32_t now = app_w800_now_ms(NULL);
                if((uint32_t)(now - g_mqtt_diag_last_ms) > 5000U)
                {
                    g_mqtt_diag_last_ms = now;
                    (void)app_w800_mqtt_publish_diag("pub-drop");
                }
            }
        }
        else if(g_mqtt_last_packet_type == 9U)
            g_mqtt_suback_seen++;
        else if(g_mqtt_last_packet_type == 13U)
            g_mqtt_pingresp_seen++;

        g_mqtt_rx_len = (uint16_t)(g_mqtt_rx_len - packet_len);
        if(g_mqtt_rx_len != 0U)
            memmove(g_mqtt_rx_stream, &g_mqtt_rx_stream[packet_len], g_mqtt_rx_len);
    }
}

static void app_w800_mqtt_stream_input(const uint8_t *data, uint16_t length)
{
    if(!data || length == 0U)
        return;

    if(length > (APP_W800_MQTT_RX_STREAM_SIZE - g_mqtt_rx_len))
    {
        g_mqtt_stream_drops++;
        g_mqtt_rx_len = 0U;
        if(length > APP_W800_MQTT_RX_STREAM_SIZE)
        {
            data += (uint16_t)(length - APP_W800_MQTT_RX_STREAM_SIZE);
            length = APP_W800_MQTT_RX_STREAM_SIZE;
        }
    }

    memcpy(&g_mqtt_rx_stream[g_mqtt_rx_len], data, length);
    g_mqtt_rx_len = (uint16_t)(g_mqtt_rx_len + length);
    app_w800_process_mqtt_stream();
}

/* app_w800_scan_json_command_bytes
 *
 * Scans a received byte block for complete JSON command objects and applies
 * them directly. This is a defensive fallback for AT modules that can deliver
 * MQTT payload bytes while the topic parser is recovering from framing noise.
 * Call only from the W800 network task context after the bytes have been copied
 * out of the AT receive path.
 */
static void app_w800_scan_json_command_bytes(const uint8_t *data, uint16_t length)
{
    const uint8_t *mark;
    const uint8_t *json_start;
    const uint8_t *json_end;
    uint16_t copy_len;
    uint16_t offset = 0U;

    if(!data || length == 0U)
        return;

    while(offset < length &&
          (mark = app_w800_find_bytes(&data[offset], (uint16_t)(length - offset), "\"cmd\"")) != NULL)
    {
        json_start = mark;
        while(json_start > data && *json_start != '{')
            json_start--;
        if(*json_start != '{')
            json_start = mark;

        json_end = mark;
        while(json_end < &data[length] && *json_end != '}')
            json_end++;

        copy_len = (uint16_t)((json_end < &data[length]) ?
                              (uint16_t)(json_end - json_start + 1U) :
                              (uint16_t)(length - (uint16_t)(json_start - data)));
        if(copy_len >= sizeof(g_network_command))
            copy_len = sizeof(g_network_command) - 1U;
        memcpy(g_network_command, json_start, copy_len);
        g_network_command[copy_len] = '\0';
        g_mqtt_last_payload_len = copy_len;
        (void)strncpy(g_mqtt_last_topic, "raw-cmd", sizeof(g_mqtt_last_topic) - 1U);
        g_mqtt_last_topic[sizeof(g_mqtt_last_topic) - 1U] = '\0';
        app_w800_apply_json_command(g_network_command);
        offset = (uint16_t)((json_end < &data[length]) ?
                            (uint16_t)(json_end - data + 1U) :
                            (uint16_t)(mark - data + 5U));
    }
}

static void app_w800_drain_input(void)
{
    int length;

    while((length = ldc_easy_pop(&g_ldc, g_input_frame, sizeof(g_input_frame))) > 0)
    {
        if(at_session_raw_is_active(&g_at_session))
        {
            at_session_input(&g_at_session, g_input_frame, (uint32_t)length);
            continue;
        }

        /* Keep raw UART AT responses out of MQTT/HTTP parsers. Network bytes
         * must enter upper protocol parsers only after the module driver's
         * socket receive path strips the AT +OK=<len> wrapper.
         */
        at_session_input(&g_at_session, g_input_frame, (uint32_t)length);
    }
}

static void app_w800_clear_rx_signal(void)
{
    while(tx_semaphore_get(&g_rx_sem, TX_NO_WAIT) == TX_SUCCESS)
    {
    }
}

static void app_w800_poll_input(void *arg)
{
    (void)arg;

    app_w800_drain_input();
    app_w800_clear_rx_signal();
    app_w800_drain_input();
}

static void app_w800_wait_input(uint32_t milliseconds, void *arg)
{
    (void)arg;

    if(ldc_easy_available(&g_ldc) == 0U)
    {
        (void)tx_semaphore_get(&g_rx_sem, app_w800_ms_to_ticks(milliseconds));
    }

    app_w800_poll_input(NULL);
}

static void app_w800_uart_rx(bsp_uart_port_t port,
                             const uint8_t *data,
                             uint16_t length,
                             void *arg)
{
    uint32_t written;

    (void)port;
    (void)arg;
    if(data && length != 0U)
    {
        written = ldc_easy_add(&g_ldc, data, length);
        if(written != (uint32_t)length)
        {
            (void)ldc_easy_abort(&g_ldc);
        }
    }
}

static bool app_w800_mqtt_connect(void)
{
    uint16_t packet_length;

    packet_length = mqtt_build_connect(g_mqtt_connect_packet,
                                       sizeof(g_mqtt_connect_packet),
                                       APP_W800_MQTT_CLIENT_ID,
                                       APP_W800_MQTT_KEEPALIVE_S);

    g_mqtt_stage = 1U;
    app_w800_log_line("w800 state: mqtt connect packet\r\n");
    if(packet_length == 0U || !at_module_send_socket(&g_module, g_mqtt_connect_packet, packet_length))
        return false;

    if(g_module.socket_id >= 0)
    {
        uint16_t actual = 0U;
        g_mqtt_stage = 2U;
        if(app_w800_socket_recv_when_ready(g_module.socket_id,
                                           g_socket_rx_payload,
                                           4U,
                                           &actual,
                                           3000U) &&
           actual != 0U)
        {
            app_w800_mqtt_stream_input(g_socket_rx_payload, actual);
            g_mqtt_stage = 3U;
        }
        else
        {
            g_mqtt_stage = 4U;
        }
    }
    return true;
}

static bool app_w800_mqtt_subscribe_commands(void)
{
    static const char *const topics[] =
    {
        APP_W800_MQTT_CMD_TOPIC,
        APP_W800_MQTT_UI_BEGIN_TOPIC,
        APP_W800_MQTT_UI_CHUNK_TOPIC,
        APP_W800_MQTT_UI_COMMIT_TOPIC
    };
    uint16_t packet_length;

    packet_length = mqtt_build_subscribe_many(g_mqtt_subscribe_packet,
                                              sizeof(g_mqtt_subscribe_packet),
                                              g_mqtt_packet_id++,
                                              topics,
                                              (uint8_t)(sizeof(topics) / sizeof(topics[0])),
                                              0U);

    g_mqtt_stage = 5U;
    if(packet_length == 0U || !at_module_send_socket(&g_module, g_mqtt_subscribe_packet, packet_length))
    {
        g_mqtt_stage = 15U;
        return false;
    }

    tx_thread_sleep(100U);
    g_mqtt_stage = 6U;
    app_w800_fetch_socket_input();
    return true;
}

static bool app_w800_mqtt_ping(void)
{
    static const uint8_t pingreq[2] = {0xC0U, 0x00U};

    return g_module.socket_id >= 0 &&
           at_module_send_socket(&g_module, pingreq, (uint16_t)sizeof(pingreq));
}

/* app_w800_mqtt_puback
 *
 * Acknowledges an inbound QoS1 PUBLISH. The application subscribes at QoS0, but
 * this keeps the parser tolerant of broker/server changes that deliver command
 * topics with QoS1.
 */
static bool app_w800_mqtt_puback(uint16_t packet_id)
{
    uint8_t puback[4];

    puback[0] = 0x40U;
    puback[1] = 0x02U;
    puback[2] = (uint8_t)(packet_id >> 8);
    puback[3] = (uint8_t)(packet_id & 0xFFU);
    return g_module.socket_id >= 0 &&
           at_module_send_socket(&g_module, puback, (uint16_t)sizeof(puback));
}

static void app_w800_http_keep_mqtt_alive(uint32_t *last_ping_ms)
{
    uint32_t now;

    if(last_ping_ms == NULL)
        return;

    now = app_w800_now_ms(NULL);
    if((uint32_t)(now - *last_ping_ms) < APP_W800_MQTT_HTTP_PING_MS)
        return;

    /*
     * HTTP Range owns the single AT UART data plane while downloading. Keep the
     * broker session alive with a sparse PINGREQ only; defer MQTT SKRCV and
     * status publishes until the HTTP transfer finishes.
     */
    (void)app_w800_mqtt_ping();
    *last_ping_ms = app_w800_now_ms(NULL);
}

static bool app_w800_mqtt_publish_status(const char *mode)
{
#if APP_W800_STATUS_MQTT_ENABLE
    uint16_t packet_length;
    int payload_len;
    ui_asset_decoder_stats_t asset_dec_stats;
    at_session_binary_diag_t at_binary_diag;
    app_health_status_t health;
    uint8_t firmware_active;
    uint32_t firmware_slot;
    uint32_t firmware_received;
    uint32_t firmware_expected;

    ui_asset_store_decoder_stats(&asset_dec_stats);
    at_session_binary_diag(&g_at_session, &at_binary_diag);
    app_health_get_status(30000U, &health);
    app_firmware_update_service_get_progress(
        &firmware_active, &firmware_slot, &firmware_received, &firmware_expected);
    memset(g_mqtt_status_payload, 0, sizeof(g_mqtt_status_payload));
    payload_len = snprintf(g_mqtt_status_payload, sizeof(g_mqtt_status_payload),
                   "{\"deviceId\":\"%s\",\"fwBuildId\":\"%s\",\"online\":true,\"mode\":\"%s\",\"broker\":\"%s:%u\","
                   "\"asset\":{\"available\":%u,\"version\":%lu,\"slot\":%u,\"received\":%lu,\"expected\":%lu,"
                   "\"error\":\"%s\",\"errorAddress\":%lu,"
                   "\"dec\":{\"info\":%lu,\"open\":%lu,\"area\":%lu,\"readFail\":%lu}},"
                   "\"firmware\":{\"active\":%u,\"slot\":%lu,\"received\":%lu,\"expected\":%lu},"
                   "\"health\":{\"required\":%lu,\"seen\":%lu,\"stale\":%lu,\"fault\":%lu,\"ticks\":%lu},"
                   "\"http\":{\"target\":%u,\"pending\":%u,\"active\":%u,\"state\":%u,\"received\":%lu,\"size\":%lu,\"error\":\"%s\"},"
                   "\"chunk\":{\"active\":%u,\"state\":%u,\"received\":%lu,\"size\":%lu,\"chunk\":%u,\"retry\":%u,"
                   "\"seen\":%lu,\"drop\":%lu,\"seqErr\":%lu,\"ofsErr\":%lu,\"b64Err\":%lu,\"crcErr\":%lu,\"error\":\"%s\"},"
                   "\"at\":{\"bin\":{\"try\":%lu,\"ok\":%lu,\"hdr\":%lu,\"cap\":%lu,\"arm\":%lu,\"to\":%lu}},"
                   "\"mqtt\":{\"pkt\":%lu,\"pub\":%lu,\"suback\":%lu,\"ping\":%lu,\"type\":%u,\"drop\":%lu,\"len\":%u,"
                   "\"plen\":%u,\"fh\":%u,\"tl\":%u,\"why\":%u,\"b\":[%u,%u,%u,%u,%u,%u],\"topic\":\"%s\"}}",
                   APP_W800_MQTT_CLIENT_ID,
                   APP_W800_FW_BUILD_ID,
                   mode ? mode : "boot",
                   APP_W800_MQTT_HOST,
                   (unsigned int)APP_W800_MQTT_PORT,
                   ui_asset_store_available() ? 1U : 0U,
                   (unsigned long)ui_asset_store_active_version(),
                   (unsigned int)ui_asset_store_active_slot(),
                   (unsigned long)ui_asset_update_received(),
                   (unsigned long)ui_asset_update_expected(),
                   ui_asset_update_error(),
                   (unsigned long)ui_asset_update_error_address(),
                   (unsigned long)asset_dec_stats.info_hits,
                   (unsigned long)asset_dec_stats.open_hits,
                   (unsigned long)asset_dec_stats.area_hits,
                   (unsigned long)asset_dec_stats.read_failures,
                   firmware_active,
                   (unsigned long)firmware_slot,
                   (unsigned long)firmware_received,
                   (unsigned long)firmware_expected,
                   (unsigned long)health.required_mask,
                   (unsigned long)health.seen_mask,
                   (unsigned long)health.stale_mask,
                   (unsigned long)health.fatal_fault,
                   (unsigned long)health.observation_ticks,
                   (unsigned int)g_http_update.target,
                   g_http_update.pending,
                   g_http_update.active,
                   (unsigned int)g_http_update.state,
                   (unsigned long)g_http_update.received,
                   (unsigned long)g_http_update.size,
                   g_http_update.error,
                   g_chunk_update.active,
                   (unsigned int)g_chunk_update.state,
                   (unsigned long)g_chunk_update.received,
                   (unsigned long)g_chunk_update.size,
                   (unsigned int)g_chunk_update.chunk_size,
                   (unsigned int)g_chunk_update.retry,
                   (unsigned long)g_chunk_update.json_seen,
                   (unsigned long)g_chunk_update.json_drop,
                   (unsigned long)g_chunk_update.seq_error,
                   (unsigned long)g_chunk_update.offset_error,
                   (unsigned long)g_chunk_update.b64_error,
                   (unsigned long)g_chunk_update.crc_error,
                   g_chunk_update.error,
                   (unsigned long)at_binary_diag.attempts,
                   (unsigned long)at_binary_diag.successes,
                   (unsigned long)at_binary_diag.header_errors,
                   (unsigned long)at_binary_diag.capacity_errors,
                   (unsigned long)at_binary_diag.raw_begin_errors,
                   (unsigned long)at_binary_diag.timeouts,
                   (unsigned long)g_mqtt_packet_seen,
                   (unsigned long)g_mqtt_publish_seen,
                   (unsigned long)g_mqtt_suback_seen,
                   (unsigned long)g_mqtt_pingresp_seen,
                   (unsigned int)g_mqtt_last_packet_type,
                   (unsigned long)g_mqtt_stream_drops,
                   (unsigned int)g_mqtt_last_payload_len,
                   (unsigned int)g_mqtt_last_packet_len,
                   (unsigned int)g_mqtt_last_fixed_header_len,
                   (unsigned int)g_mqtt_last_topic_len,
                   (unsigned int)g_mqtt_last_publish_reason,
                   (unsigned int)g_mqtt_last_b0,
                   (unsigned int)g_mqtt_last_b1,
                   (unsigned int)g_mqtt_last_b2,
                   (unsigned int)g_mqtt_last_b3,
                   (unsigned int)g_mqtt_last_b4,
                   (unsigned int)g_mqtt_last_b5,
                   g_mqtt_last_topic);
    if(payload_len <= 0 || payload_len >= (int)sizeof(g_mqtt_status_payload))
        return false;

    memset(g_mqtt_status_packet, 0, sizeof(g_mqtt_status_packet));
    packet_length = mqtt_build_publish(g_mqtt_status_packet,
                                       sizeof(g_mqtt_status_packet),
                                       APP_W800_MQTT_STATUS_TOPIC,
                                       g_mqtt_status_payload);
    app_w800_log_line("w800 state: mqtt publish status\r\n");
    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_status_packet, packet_length);
#else
    (void)mode;
    return true;
#endif
}

static bool app_w800_mqtt_publish_diag(const char *where)
{
    uint16_t packet_length;

    (void)snprintf(g_mqtt_diag_payload,
                   sizeof(g_mqtt_diag_payload),
                   "{\"w\":\"%s\",\"s\":%u,\"r\":%lu,\"e\":\"%s\",\"ae\":\"%s\",\"aa\":%lu,"
                   "\"mt\":%u,\"plen\":%u,\"fh\":%u,\"tl\":%u,\"why\":%u,"
                   "\"b\":[%u,%u,%u,%u,%u,%u],\"topic\":\"%s\"}",
                   where ? where : "",
                   (unsigned int)g_http_update.state,
                   (unsigned long)g_http_update.received,
                   g_http_update.error,
                   ui_asset_update_error(),
                   (unsigned long)ui_asset_update_error_address(),
                   (unsigned int)g_mqtt_last_packet_type,
                   (unsigned int)g_mqtt_last_packet_len,
                   (unsigned int)g_mqtt_last_fixed_header_len,
                   (unsigned int)g_mqtt_last_topic_len,
                   (unsigned int)g_mqtt_last_publish_reason,
                   (unsigned int)g_mqtt_last_b0,
                   (unsigned int)g_mqtt_last_b1,
                   (unsigned int)g_mqtt_last_b2,
                   (unsigned int)g_mqtt_last_b3,
                   (unsigned int)g_mqtt_last_b4,
                   (unsigned int)g_mqtt_last_b5,
                   g_mqtt_last_topic);

    packet_length = mqtt_build_publish(g_mqtt_diag_packet,
                                       sizeof(g_mqtt_diag_packet),
                                       APP_W800_MQTT_DIAG_TOPIC,
                                       g_mqtt_diag_payload);
    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_diag_packet, packet_length);
}

/** @brief Publish one bounded remote-operations snapshot on the dedicated topic. */
static bool app_w800_mqtt_publish_ops(uint8_t request)
{
    app_self_test_snapshot_t self_test;
    app_production_report_t production;
    app_power_snapshot_t power;
    app_can_self_test_snapshot_t can;
    app_rs485_loopback_snapshot_t rs485;
    app_blackbox_snapshot_t blackbox;
    blackbox_store_record_t tail[4];
    uint16_t tail_count;
    uint16_t packet_length;
    int payload_length;

    app_self_test_get_snapshot(&self_test);
    app_production_test_get_report(&production);
    app_power_get_snapshot(&power);
    app_can_self_test_get_snapshot(&can);
    app_rs485_get_loopback_snapshot(&rs485);
    app_blackbox_get_snapshot(&blackbox);
    tail_count = app_blackbox_read_tail(tail, 4U);

    payload_length = snprintf(
        g_mqtt_ops_payload,
        sizeof(g_mqtt_ops_payload),
        "{\"schema\":1,\"request\":%u,\"self_test\":{\"state\":%u,\"generation\":%lu,\"pass\":%u,\"fail\":%u,\"missing\":%u},"
        "\"production\":{\"state\":%u,\"session\":%lu,\"digest_valid\":%u},"
        "\"power\":{\"state\":%u,\"auto\":%u,\"locks\":%lu,\"deadlines\":%lu,\"sleep_count\":%lu,\"restore_errors\":%lu},"
        "\"can\":{\"state\":%u,\"pass\":%lu,\"fail\":%lu,\"bus_off\":[%lu,%lu],\"recovery_fail\":%lu,\"fault_state\":%u},"
        "\"rs485\":{\"requests\":%lu,\"responses\":%lu,\"pass\":%lu,\"fail\":%lu,\"registers\":[%u,%u]},"
        "\"blackbox\":{\"records\":%lu,\"io_errors\":%lu,\"dropped\":%lu,\"tail_count\":%u,\"tail_seq\":[%lu,%lu,%lu,%lu]}}",
        (unsigned int)request,
        (unsigned int)self_test.state,
        (unsigned long)self_test.generation,
        (unsigned int)self_test.passed_count,
        (unsigned int)self_test.failed_count,
        (unsigned int)(self_test.not_connected_count + self_test.not_installed_count),
        (unsigned int)production.state,
        (unsigned long)production.session_id,
        (unsigned int)production.digest_valid,
        (unsigned int)power.state,
        (unsigned int)power.auto_enabled,
        (unsigned long)power.active_lock_mask,
        (unsigned long)power.deadline_mask,
        (unsigned long)power.sleep_count,
        (unsigned long)power.restore_error_count,
        (unsigned int)can.state,
        (unsigned long)can.passed_cycles,
        (unsigned long)can.failed_cycles,
        (unsigned long)can.can1_bus_off_events,
        (unsigned long)can.can2_bus_off_events,
        (unsigned long)can.recovery_failures,
        (unsigned int)can.fault_state,
        (unsigned long)rs485.server_requests,
        (unsigned long)rs485.server_responses,
        (unsigned long)rs485.master_passes,
        (unsigned long)rs485.master_failures,
        (unsigned int)rs485.last_register_0,
        (unsigned int)rs485.last_register_1,
        (unsigned long)blackbox.store.stored_records,
        (unsigned long)blackbox.store.io_errors,
        (unsigned long)blackbox.dropped_events,
        (unsigned int)tail_count,
        (unsigned long)(tail_count > 0U ? tail[0].sequence : 0U),
        (unsigned long)(tail_count > 1U ? tail[1].sequence : 0U),
        (unsigned long)(tail_count > 2U ? tail[2].sequence : 0U),
        (unsigned long)(tail_count > 3U ? tail[3].sequence : 0U));
    if(payload_length <= 0 || payload_length >= (int)sizeof(g_mqtt_ops_payload))
        return false;

    packet_length = mqtt_build_publish(g_mqtt_ops_packet,
                                       sizeof(g_mqtt_ops_packet),
                                       APP_W800_MQTT_OPS_TOPIC,
                                       g_mqtt_ops_payload);
    return packet_length != 0U &&
           at_module_send_socket(&g_module, g_mqtt_ops_packet, packet_length);
}

static bool app_w800_mqtt_publish_modbus(app_rs485_net_payload_t type, const char *topic)
{
    uint16_t packet_length;

    if(app_rs485_format_network_payload(type, g_mqtt_modbus_payload, sizeof(g_mqtt_modbus_payload)) <= 0)
        return false;

    packet_length = mqtt_build_publish(g_mqtt_modbus_packet, sizeof(g_mqtt_modbus_packet), topic, g_mqtt_modbus_payload);
    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_modbus_packet, packet_length);
}

static bool app_w800_tcp_chunk_download_asset(void)
{
    char request[96];
    uint32_t start_ms;
    uint32_t last_progress_ms;
    uint32_t flash_crc32;
    uint32_t retry_offset = 0xFFFFFFFFUL;
    uint32_t seq = 0U;
    uint8_t block_retry_count = 0U;

    if(g_http_update.host[0] == '\0' || g_http_update.port == 0U ||
       g_http_update.size == 0U || g_http_update.size > UI_ASSET_SLOT_SIZE)
    {
        g_http_update.pending = 0U;
        return false;
    }

    g_http_update.error[0] = '\0';
    g_http_socket_id = -1;
    if(!app_w800_open_socket_id(g_http_update.host,
                                g_http_update.port,
                                app_w800_http_next_local_port(),
                                &g_http_socket_id))
    {
        app_w800_http_set_error("socket");
        g_http_update.pending = 0U;
        return false;
    }

    if(!ui_asset_update_begin(g_http_update.size, g_http_update.version))
    {
        app_w800_http_set_error("begin");
        app_w800_close_socket_id(g_http_socket_id);
        g_http_socket_id = -1;
        g_http_update.pending = 0U;
        return false;
    }

    app_w800_http_reset_stream_state();
    g_http_debug_ok_count = 0U;
    g_http_debug_ok_bytes = 0U;
    g_http_debug_payload_bytes = 0U;
    g_http_debug_stt_count = 0U;
    g_http_debug_stt_rx_max = 0U;
    g_http_debug_header_len = 0U;
    g_http_update.header_len = 0U;
    g_http_update.received = 0U;
    g_http_update.content_length = g_http_update.size;
    g_http_update.calc_crc32 = 0U;
    g_http_update.state = APP_W800_HTTP_BODY;
    g_http_update.active = 0U;

    start_ms = app_w800_now_ms(NULL);
    last_progress_ms = start_ms;

    while(g_http_update.received < g_http_update.size &&
          g_http_update.state != APP_W800_HTTP_ERROR)
    {
        uint32_t chunk_len = g_http_update.size - g_http_update.received;
        uint32_t chunk_limit = 512U;
        int request_len;
        uint8_t retry_block = 0U;

        if(g_http_socket_id < 0)
        {
            app_w800_http_set_error("closed");
            break;
        }

        if(g_http_update.received != retry_offset)
        {
            retry_offset = g_http_update.received;
            block_retry_count = 0U;
        }

        if(block_retry_count >= 4U)
            chunk_limit = 64U;
        else if(block_retry_count >= 2U)
            chunk_limit = 128U;
        else if(block_retry_count != 0U)
            chunk_limit = 256U;
        if(chunk_len > chunk_limit)
            chunk_len = chunk_limit;

        seq++;
        memset(&g_tcp_chunk_frame, 0, sizeof(g_tcp_chunk_frame));
        g_tcp_chunk_frame.offset = g_http_update.received;
        g_tcp_chunk_frame.length = chunk_len;
        g_tcp_chunk_frame.seq = seq;
        g_tcp_chunk_frame.waiting = 1U;

        request_len = snprintf(request,
                               sizeof(request),
                               "UIREQ %lu %lu %lu %lu\n",
                               (unsigned long)g_http_update.version,
                               (unsigned long)g_tcp_chunk_frame.offset,
                               (unsigned long)g_tcp_chunk_frame.length,
                               (unsigned long)g_tcp_chunk_frame.seq);
        g_http_update.active = 0U;
        if(request_len <= 0 || request_len >= (int)sizeof(request) ||
           !app_w800_send_socket_id(g_http_socket_id, (const uint8_t *)request, (uint16_t)request_len))
        {
            app_w800_http_set_error("request");
            break;
        }

        g_http_update.active = 1U;
        tx_thread_sleep(120U);
        last_progress_ms = app_w800_now_ms(NULL);
        while(g_tcp_chunk_frame.waiting != 0U &&
              g_http_update.state != APP_W800_HTTP_ERROR)
        {
            uint16_t actual = 0U;

            if(!app_w800_socket_recv_when_ready(g_http_socket_id,
                                                g_socket_rx_payload,
                                                APP_W800_HTTP_RECV_CHUNK,
                                                &actual,
                                                500U))
            {
                app_w800_http_set_error("recv cmd");
                break;
            }

            if(actual != 0U)
                app_w800_tcp_chunk_stream_input(g_socket_rx_payload, actual);

            if(g_tcp_chunk_frame.waiting == 0U)
            {
                last_progress_ms = app_w800_now_ms(NULL);
                break;
            }
            if((uint32_t)(app_w800_now_ms(NULL) - last_progress_ms) > 8000U)
            {
                if(block_retry_count < 12U)
                {
                    block_retry_count++;
                    g_http_update.active = 0U;
                    app_w800_close_socket_id(g_http_socket_id);
                    g_http_socket_id = -1;
                    app_w800_http_reset_stream_state();
                    if(!app_w800_open_socket_id(g_http_update.host,
                                                g_http_update.port,
                                                app_w800_http_next_local_port(),
                                                &g_http_socket_id))
                    {
                        app_w800_http_set_error("socket");
                        break;
                    }
                    retry_block = 1U;
                    last_progress_ms = app_w800_now_ms(NULL);
                    break;
                }
                app_w800_http_set_error("tcp wait");
                break;
            }
            tx_thread_sleep(2U);
        }

        if(retry_block != 0U)
            continue;

        block_retry_count = 0U;
        g_publish_status_requested = 1U;
        tx_thread_sleep(300U);
        if((uint32_t)(app_w800_now_ms(NULL) - start_ms) > 900000U)
        {
            app_w800_http_set_error("timeout");
            break;
        }
    }

    g_http_update.active = 0U;
    if(g_http_update.state != APP_W800_HTTP_ERROR)
    {
        if(g_http_update.crc32 != 0U && g_http_update.calc_crc32 != g_http_update.crc32)
        {
            app_w800_http_set_error("pkg crc");
        }
        else if(g_http_update.crc32 != 0U &&
                (!ui_asset_update_calculate_crc32(&flash_crc32) || flash_crc32 != g_http_update.crc32))
        {
            app_w800_http_set_error("flash crc");
        }
        else if(!ui_asset_update_commit())
        {
            app_w800_http_set_error("commit");
        }
        else
        {
            g_http_update.state = APP_W800_HTTP_DONE;
        }
    }

    app_w800_close_socket_id(g_http_socket_id);
    g_http_socket_id = -1;
    g_http_update.pending = 0U;
    (void)app_w800_mqtt_publish_diag("tcp");
    g_publish_status_requested = 1U;
    return g_http_update.state == APP_W800_HTTP_DONE;
}

static bool app_w800_http_request_once(const char *path,
                                       uint8_t mode,
                                       uint32_t range_offset,
                                       uint32_t range_length,
                                       uint32_t total_received)
{
    char request[256];
    uint32_t start_ms;
    uint32_t last_progress_ms;
    uint32_t last_received;
    int request_len;

    if(!path || path[0] == '\0' || g_http_update.host[0] == '\0' || g_http_update.port == 0U)
        return false;

    app_w800_clear_at_rx_state();
    app_w800_http_reset_stream_state();

    g_http_socket_id = -1;
    if(!app_w800_open_socket_id(g_http_update.host,
                                g_http_update.port,
                                app_w800_http_next_local_port(),
                                &g_http_socket_id))
    {
        app_w800_http_set_error("socket");
        return false;
    }

    memset(g_http_update.header, 0, sizeof(g_http_update.header));
    g_http_update.header_len = 0U;
    g_http_update.content_length = 0U;
    g_http_update.range_offset = range_offset;
    g_http_update.range_length = range_length;
    g_http_update.range_received = 0U;
    g_http_update.range_crc32 = 0U;
    g_http_update.range_calc_crc32 = 0U;
    g_http_update.raw_tcp = mode;
    g_http_update.state = APP_W800_HTTP_HEADER;
    g_http_update.active = 0U;
    app_w800_http_reset_stream_state();
    g_http_debug_ok_count = 0U;
    g_http_debug_ok_bytes = 0U;
    g_http_debug_payload_bytes = 0U;
    g_http_debug_stt_count = 0U;
    g_http_debug_stt_rx_max = 0U;
    g_http_debug_header_len = 0U;

    if(mode == APP_W800_HTTP_MODE_RANGE)
    {
        request_len = snprintf(request,
                               sizeof(request),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s:%u\r\n"
                               "User-Agent: leduo-h563-w800\r\n"
                               "Range: bytes=%lu-%lu\r\n"
                                "Connection: close\r\n"
                               "\r\n",
                               path,
                               g_http_update.host,
                               (unsigned int)g_http_update.port,
                               (unsigned long)range_offset,
                               (unsigned long)(range_offset + range_length - 1U));
    }
    else
    {
        g_http_update.received = 0U;
        request_len = snprintf(request,
                               sizeof(request),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s:%u\r\n"
                               "User-Agent: leduo-h563-w800\r\n"
                                "Connection: close\r\n"
                               "\r\n",
                               path,
                               g_http_update.host,
                               (unsigned int)g_http_update.port);
    }

    app_w800_clear_at_rx_state();
    if(request_len <= 0 || request_len >= (int)sizeof(request) ||
       !app_w800_send_socket_id(g_http_socket_id, (const uint8_t *)request, (uint16_t)request_len))
    {
        app_w800_http_set_error("request");
        app_w800_close_socket_id(g_http_socket_id);
        g_http_socket_id = -1;
        return false;
    }

    app_w800_clear_at_rx_state();
    if(!app_w800_wait_socket_rx_data(g_http_socket_id, 8000U))
    {
        app_w800_http_set_error("rx wait");
        app_w800_close_socket_id(g_http_socket_id);
        g_http_socket_id = -1;
        return false;
    }

    g_http_update.active = 1U;
    start_ms = app_w800_now_ms(NULL);
    last_progress_ms = start_ms;
    last_received = mode == APP_W800_HTTP_MODE_RANGE ? total_received : 0U;

    while(g_http_update.state != APP_W800_HTTP_DONE &&
          g_http_update.state != APP_W800_HTTP_ERROR)
    {
        if(g_http_socket_id < 0)
        {
            app_w800_http_set_error("closed");
            break;
        }

        {
            uint16_t actual = 0U;

            if(!app_w800_socket_recv_when_ready(g_http_socket_id,
                                                g_socket_rx_payload,
                                                APP_W800_HTTP_RECV_CHUNK,
                                                &actual,
                                                APP_W800_HTTP_RECV_WAIT_MS))
            {
                app_w800_http_set_error("recv cmd");
                break;
            }

            if(actual != 0U)
                app_w800_http_stream_input(g_socket_rx_payload, actual);
        }

        if(g_http_update.received != last_received)
        {
            last_received = g_http_update.received;
            last_progress_ms = app_w800_now_ms(NULL);
            g_publish_status_requested = 1U;
        }

        if((uint32_t)(app_w800_now_ms(NULL) - last_progress_ms) > 8000U ||
           (uint32_t)(app_w800_now_ms(NULL) - start_ms) > 30000U)
        {
            app_w800_http_set_error("timeout");
            break;
        }

        tx_thread_sleep(5U);
    }

    app_w800_close_socket_id(g_http_socket_id);
    g_http_socket_id = -1;
    g_http_update.active = 0U;
    return g_http_update.state == APP_W800_HTTP_DONE;
}

static bool app_w800_manifest_parse(char *asset_path,
                                    uint16_t asset_path_size,
                                    uint32_t *size,
                                    uint32_t *version,
                                    uint32_t *crc32,
                                    uint32_t *chunk_size)
{
    if(!asset_path || asset_path_size == 0U || !size || !version || !crc32 || !chunk_size)
        return false;
    if(strstr(g_http_manifest, "LEDUO_UI_ASSET") == NULL)
        return false;
    if(!app_w800_json_get_string(g_http_manifest, "path", asset_path, asset_path_size))
        return false;
    if(!app_w800_json_get_u32(g_http_manifest, "size", size) ||
       !app_w800_json_get_u32(g_http_manifest, "version", version) ||
       !app_w800_json_get_u32(g_http_manifest, "crc32", crc32))
    {
        return false;
    }
    *chunk_size = APP_W800_HTTP_RANGE_DEFAULT;
    (void)app_w800_json_get_u32(g_http_manifest, "chunkSize", chunk_size);
    if(*chunk_size > APP_W800_HTTP_RANGE_MAX)
        *chunk_size = APP_W800_HTTP_RANGE_MAX;
    if(*chunk_size < APP_W800_HTTP_RANGE_MIN)
        *chunk_size = APP_W800_HTTP_RANGE_DEFAULT;
    return *size != 0U && *size <= UI_ASSET_SLOT_SIZE && *version != 0U;
}

static bool app_w800_http_direct_range_download_asset(void)
{
    char asset_path[80];
    uint32_t chunk_size = g_http_direct_chunk_size;
    uint32_t last_mqtt_ping_ms = app_w800_now_ms(NULL);
    uint32_t flash_crc32;
    uint8_t retry_count = 0U;
    ota_firmware_descriptor_t firmware_descriptor;
    bool firmware_target = g_http_update.target == APP_W800_HTTP_TARGET_FIRMWARE;

#if !APP_ENABLE_FIRMWARE_UPDATE
    if(firmware_target)
    {
        app_w800_http_set_error("fw update disabled");
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return false;
    }
#endif

    strncpy(asset_path, g_http_update.path, sizeof(asset_path) - 1U);
    asset_path[sizeof(asset_path) - 1U] = '\0';
    if(asset_path[0] == '\0')
        (void)strncpy(asset_path,
                      firmware_target ? "/firmware/app.bin" : "/ui/ui_assets.bin",
                      sizeof(asset_path) - 1U);

    if(chunk_size > APP_W800_HTTP_RANGE_MAX)
        chunk_size = APP_W800_HTTP_RANGE_MAX;
    if(chunk_size < APP_W800_HTTP_RANGE_MIN)
        chunk_size = APP_W800_HTTP_RANGE_DEFAULT;

    if(g_http_update.size == 0U ||
       g_http_update.size > (firmware_target ? OTA_EXT_FIRMWARE_SLOT_SIZE : UI_ASSET_SLOT_SIZE) ||
       g_http_update.version == 0U ||
       g_http_update.crc32 == 0U ||
       (firmware_target &&
        (g_http_update.entry_address < OTA_APP_BASE ||
         g_http_update.entry_address >= (OTA_APP_BASE + OTA_APP_SIZE) ||
         (g_http_update.entry_address & 1U) == 0U)))
    {
        app_w800_http_set_error("manifest");
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return false;
    }

    if(!firmware_target &&
       ui_asset_store_available() && ui_asset_store_active_version() == g_http_update.version)
    {
        g_http_update.pending = 0U;
        g_http_update.state = APP_W800_HTTP_DONE;
        g_publish_status_requested = 1U;
        return true;
    }

    memset(&firmware_descriptor, 0, sizeof(firmware_descriptor));
    firmware_descriptor.state = (uint32_t)OTA_SLOT_STATE_VERIFIED;
    firmware_descriptor.image_version = g_http_update.version;
    firmware_descriptor.image_size = g_http_update.size;
    firmware_descriptor.image_crc32 = g_http_update.crc32;
    firmware_descriptor.image_flags = g_http_update.image_flags;
    firmware_descriptor.load_address = OTA_APP_BASE;
    firmware_descriptor.entry_address = g_http_update.entry_address;
    memcpy(firmware_descriptor.image_sha256, g_http_update.image_sha256,
           sizeof(firmware_descriptor.image_sha256));
    memcpy(firmware_descriptor.signature, g_http_update.signature,
           sizeof(firmware_descriptor.signature));

    if(firmware_target)
    {
        ota_firmware_update_status_t begin_status =
            app_firmware_update_service_begin(&firmware_descriptor);

        if(begin_status != OTA_FIRMWARE_UPDATE_OK)
        {
            app_w800_http_set_error(
                begin_status == OTA_FIRMWARE_UPDATE_VERSION_ROLLBACK ?
                "version rollback" : "begin");
            g_http_update.pending = 0U;
            g_publish_status_requested = 1U;
            return false;
        }
    }
    else if(!ui_asset_update_begin(g_http_update.size, g_http_update.version))
    {
        app_w800_http_set_error("begin");
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return false;
    }

    g_http_update.received = 0U;
    g_http_update.calc_crc32 = 0U;

    while(g_http_update.received < g_http_update.size)
    {
        uint32_t remaining = g_http_update.size - g_http_update.received;
        uint32_t request_len = chunk_size;

        if(retry_count >= 4U)
            request_len = APP_W800_HTTP_RANGE_MIN;
        else if(retry_count >= 2U && request_len > 128U)
            request_len = 128U;
        else if(retry_count != 0U && request_len > 256U)
            request_len = 256U;

        if(request_len > remaining)
            request_len = remaining;

        g_http_update.error[0] = '\0';
        g_http_update.state = APP_W800_HTTP_IDLE;
        if(app_w800_http_request_once(asset_path,
                                      APP_W800_HTTP_MODE_RANGE,
                                      g_http_update.received,
                                      request_len,
                                      g_http_update.received))
        {
            retry_count = 0U;
            app_w800_http_keep_mqtt_alive(&last_mqtt_ping_ms);
            continue;
        }

        app_w800_http_keep_mqtt_alive(&last_mqtt_ping_ms);
        if(retry_count++ >= APP_W800_HTTP_RANGE_RETRY_LIMIT)
        {
            app_w800_http_set_error("range retry");
            break;
        }
        tx_thread_sleep(200U);
    }

    if(g_http_update.received >= g_http_update.size &&
       g_http_update.calc_crc32 == g_http_update.crc32 &&
       ((firmware_target &&
         app_firmware_update_service_finish() == OTA_FIRMWARE_UPDATE_OK) ||
        (!firmware_target &&
         ui_asset_update_calculate_crc32(&flash_crc32) &&
         flash_crc32 == g_http_update.crc32 &&
         (ui_asset_update_commit() ||
          (ui_asset_store_available() &&
           ui_asset_store_active_version() == g_http_update.version)))))
    {
        g_http_update.state = APP_W800_HTTP_DONE;
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        if(firmware_target)
            g_firmware_reboot_pending = 1U;
        return true;
    }

    if(firmware_target)
        (void)app_firmware_update_service_abort();
    if(g_http_update.error[0] == '\0')
        app_w800_http_set_error(g_http_update.calc_crc32 == g_http_update.crc32 ? "flash crc" : "crc");
    g_http_update.pending = 0U;
    g_publish_status_requested = 1U;
    return false;
}

static bool app_w800_http_manifest_download_asset(void)
{
    char manifest_path[80];
    char asset_path[80];
    uint32_t size;
    uint32_t version;
    uint32_t crc32;
    uint32_t chunk_size;
    uint32_t last_mqtt_ping_ms;
    uint32_t flash_crc32;
    uint8_t retry_count = 0U;

    strncpy(manifest_path, g_http_update.path, sizeof(manifest_path) - 1U);
    manifest_path[sizeof(manifest_path) - 1U] = '\0';

    g_http_update.error[0] = '\0';
    g_http_update.received = 0U;
    g_http_update.calc_crc32 = 0U;
    g_http_manifest_len = 0U;
    g_http_manifest[0] = '\0';

    if(!app_w800_http_request_once(manifest_path, APP_W800_HTTP_MODE_MANIFEST, 0U, 0U, 0U))
    {
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return false;
    }

    if(!app_w800_manifest_parse(asset_path,
                                sizeof(asset_path),
                                &size,
                                &version,
                                &crc32,
                                &chunk_size))
    {
        app_w800_http_set_error("manifest");
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return false;
    }

    if(ui_asset_store_available() && ui_asset_store_active_version() == version)
    {
        g_http_update.pending = 0U;
        g_http_update.state = APP_W800_HTTP_DONE;
        g_publish_status_requested = 1U;
        return true;
    }

    if(!ui_asset_update_begin(size, version))
    {
        app_w800_http_set_error("begin");
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return false;
    }

    g_http_update.size = size;
    g_http_update.version = version;
    g_http_update.crc32 = crc32;
    g_http_update.received = 0U;
    g_http_update.calc_crc32 = 0U;
    last_mqtt_ping_ms = app_w800_now_ms(NULL);

    while(g_http_update.received < g_http_update.size)
    {
        uint32_t remaining = g_http_update.size - g_http_update.received;
        uint32_t request_len = chunk_size;

        if(retry_count >= 4U)
            request_len = APP_W800_HTTP_RANGE_MIN;
        else if(retry_count >= 2U && request_len > 128U)
            request_len = 128U;
        else if(retry_count != 0U && request_len > 256U)
            request_len = 256U;

        if(request_len > remaining)
            request_len = remaining;

        g_http_update.error[0] = '\0';
        g_http_update.state = APP_W800_HTTP_IDLE;
        if(app_w800_http_request_once(asset_path,
                                      APP_W800_HTTP_MODE_RANGE,
                                      g_http_update.received,
                                      request_len,
                                      g_http_update.received))
        {
            retry_count = 0U;
            app_w800_http_keep_mqtt_alive(&last_mqtt_ping_ms);
            continue;
        }

        app_w800_http_keep_mqtt_alive(&last_mqtt_ping_ms);
        if(retry_count++ >= APP_W800_HTTP_RANGE_RETRY_LIMIT)
        {
            app_w800_http_set_error("range retry");
            break;
        }
        tx_thread_sleep(200U);
    }

    if(g_http_update.received >= g_http_update.size &&
       g_http_update.calc_crc32 == g_http_update.crc32 &&
       ui_asset_update_calculate_crc32(&flash_crc32) &&
       flash_crc32 == g_http_update.crc32 &&
       (ui_asset_update_commit() ||
        (ui_asset_store_available() && ui_asset_store_active_version() == g_http_update.version)))
    {
        g_http_update.state = APP_W800_HTTP_DONE;
        g_http_update.pending = 0U;
        g_publish_status_requested = 1U;
        return true;
    }

    if(g_http_update.error[0] == '\0')
        app_w800_http_set_error(g_http_update.calc_crc32 == g_http_update.crc32 ? "flash crc" : "crc");
    g_http_update.pending = 0U;
    g_publish_status_requested = 1U;
    return false;
}

static bool app_w800_http_download_asset(void)
{
    char request[192];
    uint32_t start_ms;
    uint32_t last_progress_ms;
    uint32_t last_received;
    int request_len;

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_TCP_CHUNK)
        return app_w800_tcp_chunk_download_asset();

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_MANIFEST)
        return app_w800_http_manifest_download_asset();

    if(g_http_update.raw_tcp == APP_W800_HTTP_MODE_RANGE)
        return app_w800_http_direct_range_download_asset();

    if(g_http_update.host[0] == '\0' ||
       (g_http_update.raw_tcp == APP_W800_HTTP_MODE_FULL && g_http_update.path[0] == '\0') ||
       g_http_update.port == 0U)
    {
        g_http_update.pending = 0U;
        return false;
    }

    g_http_socket_id = -1;
    if(!app_w800_open_socket_id(g_http_update.host,
                                g_http_update.port,
                                app_w800_http_next_local_port(),
                                &g_http_socket_id))
    {
        app_w800_http_set_error("socket");
        g_http_update.pending = 0U;
        return false;
    }

    memset(g_http_update.header, 0, sizeof(g_http_update.header));
    g_http_update.header_len = 0U;
    g_http_update.received = 0U;
    g_http_update.content_length = 0U;
    g_http_update.calc_crc32 = 0U;
    g_http_update.state = g_http_update.raw_tcp != APP_W800_HTTP_MODE_FULL ? APP_W800_HTTP_BODY : APP_W800_HTTP_HEADER;
    g_http_update.active = 0U;
    app_w800_http_reset_stream_state();

    if(g_http_update.raw_tcp != APP_W800_HTTP_MODE_FULL)
    {
        if(g_http_update.size == 0U || !ui_asset_update_begin(g_http_update.size, g_http_update.version))
        {
            app_w800_http_set_error("begin");
            app_w800_close_socket_id(g_http_socket_id);
            g_http_socket_id = -1;
            g_http_update.pending = 0U;
            return false;
        }
        g_http_update.content_length = g_http_update.size;
        g_http_update.active = 1U;
    }
    else
    {
        request_len = snprintf(request,
                               sizeof(request),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s:%u\r\n"
                               "User-Agent: leduo-h563-w800\r\n"
                               "Connection: close\r\n"
                               "\r\n",
                               g_http_update.path,
                               g_http_update.host,
                               (unsigned int)g_http_update.port);
        if(request_len <= 0 || request_len >= (int)sizeof(request) ||
           !app_w800_send_socket_id(g_http_socket_id, (const uint8_t *)request, (uint16_t)request_len))
        {
            app_w800_http_set_error("request");
            app_w800_close_socket_id(g_http_socket_id);
            g_http_socket_id = -1;
            g_http_update.pending = 0U;
            return false;
        }

        g_http_update.active = 1U;
    }

    start_ms = app_w800_now_ms(NULL);
    last_progress_ms = start_ms;
    last_received = 0U;

    while(g_http_update.state != APP_W800_HTTP_DONE &&
          g_http_update.state != APP_W800_HTTP_ERROR)
    {
        if(g_http_socket_id < 0)
        {
            app_w800_http_set_error("closed");
            break;
        }

        {
            uint16_t actual = 0U;

            if(!app_w800_socket_recv_when_ready(g_http_socket_id,
                                                g_socket_rx_payload,
                                                APP_W800_HTTP_RECV_CHUNK,
                                                &actual,
                                                APP_W800_HTTP_RECV_WAIT_MS))
            {
                app_w800_http_set_error("recv cmd");
                break;
            }

            if(actual != 0U)
                app_w800_http_stream_input(g_socket_rx_payload, actual);
        }

        if(g_http_update.received != last_received)
        {
            last_received = g_http_update.received;
            last_progress_ms = app_w800_now_ms(NULL);
            g_publish_status_requested = 1U;
        }

        if((uint32_t)(app_w800_now_ms(NULL) - last_progress_ms) > 10000U ||
           (uint32_t)(app_w800_now_ms(NULL) - start_ms) > 900000U)
        {
            app_w800_http_set_error("timeout");
            break;
        }

        tx_thread_sleep(10U);
    }

    app_w800_close_socket_id(g_http_socket_id);
    g_http_socket_id = -1;
    g_http_update.active = 0U;
    g_http_update.pending = 0U;
    g_publish_status_requested = 1U;
    return g_http_update.state == APP_W800_HTTP_DONE;
}

static void app_w800_fetch_socket_input(void)
{
    uint16_t actual = 0U;

    if(g_module.socket_id < 0)
        return;

    if(app_w800_socket_recv_when_ready(g_module.socket_id,
                                       g_socket_rx_payload,
                                       1024U,
                                       &actual,
                                       1U) &&
       actual != 0U)
    {
        app_w800_mqtt_stream_input(g_socket_rx_payload, actual);
        app_w800_scan_json_command_bytes(g_socket_rx_payload, actual);
    }
}

static uint16_t app_w800_next_local_port(void)
{
    uint16_t port = g_local_port++;

    if(g_local_port > APP_W800_LOCAL_PORT_END)
        g_local_port = APP_W800_LOCAL_PORT_START;
    return port;
}

static uint16_t app_w800_http_next_local_port(void)
{
    uint16_t port = g_http_local_port++;

    if(g_http_local_port > APP_W800_HTTP_LOCAL_PORT_END)
        g_http_local_port = APP_W800_HTTP_LOCAL_PORT_START;
    return port;
}

UINT app_w800_init(void)
{
    ldc_easy_config_t ldc_config;
    UINT status;

    status = tx_mutex_create(&g_usb_credentials_mutex,
                             "w800 usb credentials",
                             TX_INHERIT);
    if(status != TX_SUCCESS)
        return status;
    g_usb_credentials_mutex_ready = 1U;

    status = tx_semaphore_create(&g_rx_sem, "w800 rx", 0U);
    if(status != TX_SUCCESS)
        return status;

    memset(&ldc_config, 0, sizeof(ldc_config));
    ldc_config.ring_buffer = g_ldc_ring;
    ldc_config.ring_size = sizeof(g_ldc_ring);
    ldc_config.packet_pool = g_packets;
    ldc_config.packet_count = APP_W800_PACKET_COUNT;
    ldc_config.max_frame = APP_W800_LDC_MAX_FRAME;
    ldc_config.timeout_ms = APP_W800_LDC_IDLE_TIMEOUT_MS;
    /* The W800 AT stream carries both text lines and SKRCV binary payloads on
     * the same UART. Leave delimiter framing disabled here; AT core parses CR/LF
     * text itself, and binary payloads may legitimately contain many LF bytes.
     */
    ldc_config.delimiter_enabled = false;
    ldc_config.delimiter = 0U;
    ldc_config.mode = LDC_MODE_OVERWRITE;
    ldc_config.lock = ldc_port_irq_lock;
    ldc_config.unlock = ldc_port_irq_unlock;
    ldc_config.event_cb = app_w800_ldc_event;
    ldc_config.auto_tick = true;
    if(!ldc_easy_init(&g_ldc, &ldc_config))
        return TX_START_ERROR;

    at_session_init(&g_at_session,
                    app_w800_at_tx,
                    NULL,
                    app_w800_now_ms,
                    NULL,
                    app_w800_wait_input,
                    NULL);
    at_session_set_logger(&g_at_session, app_w800_at_log, NULL);
    at_session_set_poll_callback(&g_at_session, app_w800_poll_input, NULL);
    at_module_init(&g_module, &g_at_session, &g_at_module_w800, NULL);

    if(bsp_uart_register_rx_callback(BSP_UART_W800_AT, app_w800_uart_rx, NULL) != 0 ||
       bsp_uart_start_rx(BSP_UART_W800_AT, g_uart_rx_buffer, sizeof(g_uart_rx_buffer)) != 0)
        return TX_START_ERROR;
    return TX_SUCCESS;
}

static volatile uint8_t g_power_pause_requested;
static volatile uint8_t g_power_paused;

/** @brief Request and wait for the W800 task to reach its cooperative idle point. */
bool app_w800_pause(uint32_t timeout_ms)
{
    uint32_t start_ms = bsp_timer_get_ms();

    g_power_pause_requested = 1U;
    while(g_power_paused == 0U)
    {
        if((bsp_timer_get_ms() - start_ms) >= timeout_ms)
        {
            g_power_pause_requested = 0U;
            return false;
        }
        tx_thread_sleep(1U);
    }
    return true;
}

/** @brief Release the W800 task after clocks and reception are restored. */
void app_w800_resume(void)
{
    g_power_pause_requested = 0U;
}

/** @brief Run the W800 state machine and honor cooperative power pauses. */
void app_w800_task_entry(ULONG thread_input)
{
    app_w800_state_t state = APP_W800_STATE_RESET;
    app_w800_state_t reported_state = APP_W800_STATE_RESET;
    bool state_reported = false;
    bool wifi_ready = false;
    bool spi_nor_logged = false;
    uint32_t last_command_fetch_ms = 0U;
    uint32_t last_heartbeat_ms = 0U;
    uint32_t state_entered_ms = 0U;
    char usb_ssid[APP_W800_SSID_MAX_LENGTH + 1U];
    char usb_password[APP_W800_PASSWORD_MAX_LENGTH + 1U];

    (void)thread_input;
    for(;;)
    {
        if(g_power_pause_requested != 0U)
        {
            g_power_paused = 1U;
            while(g_power_pause_requested != 0U)
            {
                tx_thread_sleep(1U);
            }
            g_power_paused = 0U;
        }
        app_health_report(APP_HEALTH_SERVICE_W800);
        g_wifi_ready = wifi_ready ? 1U : 0U;
        g_mqtt_online = state == APP_W800_STATE_ONLINE ? 1U : 0U;
        g_state_diag = (uint8_t)state;
        if(!state_reported || state != reported_state)
        {
            reported_state = state;
            state_reported = true;
        }

        if(app_w800_take_usb_credentials(usb_ssid, usb_password))
        {
            (void)at_module_close_socket(&g_module);
            (void)at_module_w800_stop_provision(&g_module);
            wifi_ready = false;
            g_mqtt_online = 0U;
            g_provisioning_active = 0U;
            g_usb_rescue_attempts++;
            if(at_module_w800_save_station_profile(&g_module,
                                                    usb_ssid,
                                                    usb_password))
            {
                g_usb_rescue_state = APP_W800_USB_RESCUE_SAVED;
                state = APP_W800_STATE_RESET;
                state_entered_ms = app_w800_now_ms(NULL);
            }
            else
            {
                g_usb_rescue_state = APP_W800_USB_RESCUE_FAILED;
                state = APP_W800_STATE_PROVISION_START;
                state_entered_ms = app_w800_now_ms(NULL);
            }
            app_w800_secure_zero(usb_ssid, sizeof(usb_ssid));
            app_w800_secure_zero(usb_password, sizeof(usb_password));
            continue;
        }

        if(g_reconnect_requested != 0U)
        {
            g_reconnect_requested = 0U;
            state = wifi_ready ? APP_W800_STATE_MQTT_RETRY : APP_W800_STATE_RESET;
            g_mqtt_online = 0U;
        }

        if(g_provision_requested != 0U)
        {
            g_provision_requested = 0U;
            (void)at_module_close_socket(&g_module);
            wifi_ready = false;
            g_mqtt_online = 0U;
            g_provisioning_active = 0U;
            state = APP_W800_STATE_PROVISION_START;
            state_entered_ms = app_w800_now_ms(NULL);
        }

        if(APP_ENABLE_W800_LOG && !spi_nor_logged && usb_console_is_connected())
        {
            bsp_spi_nor_log_id(app_w800_log_line);
            spi_nor_logged = true;
        }

        switch(state)
        {
        case APP_W800_STATE_RESET:
            (void)at_module_close_socket(&g_module);
            (void)at_module_reset(&g_module);
            wifi_ready = false;
            g_provisioning_active = 0U;
            state_entered_ms = app_w800_now_ms(NULL);
            state = APP_W800_STATE_WIFI_RESTORE;
            break;

        case APP_W800_STATE_WIFI_RESTORE:
            if(at_module_is_network_ready(&g_module))
            {
                wifi_ready = true;
                if(g_usb_rescue_state == APP_W800_USB_RESCUE_SAVED)
                    g_usb_rescue_state = APP_W800_USB_RESCUE_CONNECTED;
                app_w800_log_line("w800 state: saved wifi ready\r\n");
                state = APP_W800_STATE_MQTT_SOCKET;
            }
            else if((uint32_t)(app_w800_now_ms(NULL) - state_entered_ms) >=
                    APP_W800_RESTORE_TIMEOUT_MS)
            {
                app_w800_log_line("w800 state: saved wifi unavailable, ble provision\r\n");
                state = APP_W800_STATE_PROVISION_START;
            }
            else
            {
                tx_thread_sleep(APP_W800_PROVISION_POLL_MS);
            }
            break;

        case APP_W800_STATE_PROVISION_START:
            (void)at_module_close_socket(&g_module);
            g_provision_attempts++;
            if(at_module_w800_start_ble_provision(&g_module))
            {
                g_provisioning_active = 1U;
                state_entered_ms = app_w800_now_ms(NULL);
                app_w800_log_line("w800 state: ble provision active\r\n");
                state = APP_W800_STATE_PROVISION_WAIT;
            }
            else
            {
                g_provisioning_active = 0U;
                app_w800_log_line("w800 error: ble provision start failed\r\n");
                tx_thread_sleep(APP_W800_PROVISION_RETRY_MS);
                state = APP_W800_STATE_RESET;
            }
            break;

        case APP_W800_STATE_PROVISION_WAIT:
            if(at_module_is_network_ready(&g_module))
            {
                (void)at_module_w800_stop_provision(&g_module);
                g_provisioning_active = 0U;
                wifi_ready = true;
                app_w800_log_line("w800 state: ble provision complete\r\n");
                state = APP_W800_STATE_MQTT_SOCKET;
            }
            else if((uint32_t)(app_w800_now_ms(NULL) - state_entered_ms) >=
                    APP_W800_PROVISION_TIMEOUT_MS)
            {
                (void)at_module_w800_stop_provision(&g_module);
                g_provisioning_active = 0U;
                g_provision_timeouts++;
                app_w800_log_line("w800 warn: ble provision timeout\r\n");
                tx_thread_sleep(APP_W800_PROVISION_RETRY_MS);
                state = APP_W800_STATE_RESET;
            }
            else
            {
                tx_thread_sleep(APP_W800_PROVISION_POLL_MS);
            }
            break;

        case APP_W800_STATE_MQTT_SOCKET:
        {
            const at_socket_config_t socket_config =
            {
                APP_W800_MQTT_HOST,
                APP_W800_MQTT_PORT,
                app_w800_next_local_port()
            };

            (void)at_module_close_socket(&g_module);
            if(at_module_open_socket(&g_module, &socket_config))
                state = APP_W800_STATE_MQTT_CONNECT;
            else
            {
                app_w800_log_line("w800 error: tcp socket failed\r\n");
                tx_thread_sleep(3000U);
                state = APP_W800_STATE_MQTT_RETRY;
            }
            break;
        }

        case APP_W800_STATE_MQTT_CONNECT:
            if(app_w800_mqtt_connect())
            {
                if(!app_w800_mqtt_subscribe_commands())
                {
                    app_w800_log_line("w800 error: mqtt subscribe failed\r\n");
                    tx_thread_sleep(3000U);
                    state = APP_W800_STATE_MQTT_RETRY;
                    break;
                }
                tx_thread_sleep(200U);
                app_w800_fetch_socket_input();
                last_command_fetch_ms = app_w800_now_ms(NULL);
                last_heartbeat_ms = last_command_fetch_ms;
                (void)app_w800_mqtt_publish_modbus(APP_RS485_NET_CONFIG, APP_W800_MQTT_CONFIG_TOPIC);
                g_mqtt_stage = 7U;
                if(app_w800_mqtt_publish_status("online"))
                    app_w800_log_line("w800 state: mqtt online\r\n");
                else
                    app_w800_log_line("w800 warn: mqtt status publish failed\r\n");
                g_mqtt_stage = 8U;
                state = APP_W800_STATE_ONLINE;
            }
            else
            {
                app_w800_log_line("w800 error: mqtt connect send failed\r\n");
                tx_thread_sleep(3000U);
                state = APP_W800_STATE_MQTT_RETRY;
            }
            break;

        case APP_W800_STATE_ONLINE:
        {
            uint32_t now = app_w800_now_ms(NULL);
            app_self_test_snapshot_t remote_self_test;
            app_production_report_t remote_production;
            uint32_t fetch_period = app_w800_ui_update_in_progress() ?
                                    APP_W800_MQTT_ACTIVE_FETCH_MS :
                                    APP_W800_MQTT_IDLE_FETCH_MS;

            if(g_http_update.pending != 0U)
            {
                (void)app_w800_http_download_asset();
                last_command_fetch_ms = app_w800_now_ms(NULL);
                g_publish_status_requested = 1U;
                break;
            }

            if(g_firmware_reboot_pending != 0U)
            {
                g_firmware_reboot_pending = 0U;
                (void)app_w800_mqtt_publish_status("firmware-ready");
                tx_thread_sleep(1000U);
                bsp_system_reset();
                break;
            }

            app_w800_poll_input(NULL);
            if((uint32_t)(now - last_command_fetch_ms) >= fetch_period)
            {
                app_w800_fetch_socket_input();
                last_command_fetch_ms = app_w800_now_ms(NULL);
                now = last_command_fetch_ms;
            }

            app_w800_ui_chunk_step(now);

            if(g_remote_self_test_watch != 0U)
            {
                app_self_test_get_snapshot(&remote_self_test);
                if(remote_self_test.state == APP_SELF_TEST_STATE_COMPLETED &&
                   remote_self_test.generation != g_remote_self_test_generation)
                {
                    g_remote_self_test_watch = 0U;
                    g_publish_ops_requested = APP_W800_OPS_DIAGNOSTICS;
                }
            }
            if(g_remote_production_watch != 0U)
            {
                app_production_test_get_report(&remote_production);
                if(remote_production.state == APP_PRODUCTION_STATE_PASSED ||
                   remote_production.state == APP_PRODUCTION_STATE_FAILED)
                {
                    g_remote_production_watch = 0U;
                    g_publish_ops_requested = APP_W800_OPS_PRODUCTION;
                }
            }

            if(g_chunk_update.active != 0U)
            {
                g_publish_status_requested = 0U;
                g_publish_config_requested = 0U;
                g_publish_data_requested = 0U;
                tx_thread_sleep(10U);
                break;
            }

            if(g_publish_status_requested != 0U)
            {
                g_publish_status_requested = 0U;
                (void)app_w800_mqtt_publish_status("requested");
            }
            if(g_publish_config_requested != 0U)
            {
                g_publish_config_requested = 0U;
                (void)app_w800_mqtt_publish_modbus(APP_RS485_NET_CONFIG, APP_W800_MQTT_CONFIG_TOPIC);
            }
            if(g_publish_data_requested != 0U)
            {
                g_publish_data_requested = 0U;
                (void)app_w800_mqtt_publish_modbus(APP_RS485_NET_DATA, APP_W800_MQTT_DATA_TOPIC);
            }
            if(g_publish_ops_requested != APP_W800_OPS_NONE)
            {
                uint8_t request = g_publish_ops_requested;

                g_publish_ops_requested = APP_W800_OPS_NONE;
                (void)app_w800_mqtt_publish_ops(request);
            }
            if(app_w800_ui_update_in_progress())
            {
                tx_thread_sleep(20U);
                break;
            }
            if((uint32_t)(now - last_heartbeat_ms) >= APP_W800_MQTT_HEARTBEAT_MS)
            {
                last_heartbeat_ms = now;
                if(!app_w800_mqtt_publish_status("heartbeat"))
                {
                    app_w800_log_line("w800 warn: mqtt heartbeat failed, reconnect\r\n");
                    state = APP_W800_STATE_MQTT_RETRY;
                }
                else
                {
                    (void)app_w800_mqtt_publish_modbus(APP_RS485_NET_DATA, APP_W800_MQTT_DATA_TOPIC);
                }
            }
            tx_thread_sleep(20U);
            break;
        }

        case APP_W800_STATE_MQTT_RETRY:
            (void)at_module_close_socket(&g_module);
            if(at_module_is_network_ready(&g_module))
            {
                wifi_ready = true;
                app_w800_log_line("w800 state: mqtt retry keep wifi\r\n");
                tx_thread_sleep(1000U);
                state = APP_W800_STATE_MQTT_SOCKET;
            }
            else
            {
                wifi_ready = false;
                app_w800_log_line("w800 warn: wifi lost, restore saved profile\r\n");
                tx_thread_sleep(1000U);
                state = APP_W800_STATE_RESET;
            }
            break;

        default:
            state = APP_W800_STATE_RESET;
            break;
        }
    }
}

/** @brief Apply one bounded JSON command through the same path as MQTT input. */
bool app_w800_apply_remote_json(const char *json)
{
    char command[24];

    if(json == NULL ||
       !app_w800_json_get_string(json, "cmd", command, sizeof(command)))
    {
        return false;
    }

    app_w800_apply_json_command(json);
    return true;
}

void app_w800_get_status(app_w800_status_t *status)
{
    if(!status)
        return;
    status->wifi_ready = g_wifi_ready;
    status->mqtt_online = g_mqtt_online;
    status->provisioning_active = g_provisioning_active;
    status->provision_attempts = g_provision_attempts;
    status->provision_timeouts = g_provision_timeouts;
    status->usb_rescue_state = g_usb_rescue_state;
    status->usb_rescue_attempts = g_usb_rescue_attempts;
    status->socket_id = g_module.socket_id;
    status->state = g_state_diag;
    status->mqtt_stage = g_mqtt_stage;
    status->socket_local_port = g_socket_local_port;
    status->socket_rx_data = g_socket_rx_data;
    status->socket_recv_result = g_socket_recv_result;
    status->socket_recv_actual = g_socket_recv_actual;
    status->socket_recv_fail_count = g_socket_recv_fail_count;
    status->socket_recv_head[0] = g_socket_recv_head[0];
    status->socket_recv_head[1] = g_socket_recv_head[1];
    status->socket_recv_head[2] = g_socket_recv_head[2];
    status->socket_recv_head[3] = g_socket_recv_head[3];
    status->mqtt_publish_seen = g_mqtt_publish_seen;
    status->mqtt_begin_seen = g_mqtt_begin_seen;
    status->mqtt_chunk_seen = g_mqtt_chunk_seen;
    status->mqtt_commit_seen = g_mqtt_commit_seen;
    status->mqtt_stream_drops = g_mqtt_stream_drops;
    status->mqtt_last_payload_len = g_mqtt_last_payload_len;
    (void)strncpy(status->mqtt_last_topic, g_mqtt_last_topic, sizeof(status->mqtt_last_topic) - 1U);
    status->mqtt_last_topic[sizeof(status->mqtt_last_topic) - 1U] = '\0';
    status->http_pending = g_http_update.pending;
    status->http_active = g_http_update.active;
    status->http_state = (uint8_t)g_http_update.state;
    status->http_received = g_http_update.received;
    status->http_size = g_http_update.size;
    (void)strncpy(status->http_error, g_http_update.error, sizeof(status->http_error) - 1U);
    status->http_error[sizeof(status->http_error) - 1U] = '\0';
    status->chunk_active = g_chunk_update.active;
    status->chunk_state = (uint8_t)g_chunk_update.state;
    status->chunk_received = g_chunk_update.received;
    status->chunk_size = g_chunk_update.size;
    status->chunk_unit = g_chunk_update.chunk_size;
    status->chunk_retry = g_chunk_update.retry;
    status->chunk_json_seen = g_chunk_update.json_seen;
    status->chunk_json_drop = g_chunk_update.json_drop;
    status->chunk_seq_error = g_chunk_update.seq_error;
    status->chunk_offset_error = g_chunk_update.offset_error;
    status->chunk_b64_error = g_chunk_update.b64_error;
    status->chunk_crc_error = g_chunk_update.crc_error;
    (void)strncpy(status->chunk_error, g_chunk_update.error, sizeof(status->chunk_error) - 1U);
    status->chunk_error[sizeof(status->chunk_error) - 1U] = '\0';
    (void)ldc_easy_get_stats(&g_ldc, &status->ldc);

}

void app_w800_request_reconnect(void)
{
    g_reconnect_requested = 1U;
}

/** @brief Request a bounded transition into W800 BLE provisioning. */
void app_w800_request_ble_provisioning(void)
{
    g_provision_requested = 1U;
}

app_w800_credentials_result_t app_w800_request_usb_credentials(const char *ssid,
                                                               const char *password)
{
    if(!app_w800_credential_is_valid(ssid, 1U, APP_W800_SSID_MAX_LENGTH))
        return APP_W800_CREDENTIALS_INVALID_SSID;
    if(!app_w800_credential_is_valid(password, 8U, APP_W800_PASSWORD_MAX_LENGTH))
        return APP_W800_CREDENTIALS_INVALID_PASSWORD;
    if(g_usb_credentials_mutex_ready == 0U)
        return APP_W800_CREDENTIALS_NOT_READY;
    if(tx_mutex_get(&g_usb_credentials_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        return APP_W800_CREDENTIALS_NOT_READY;

    if(g_usb_credentials_pending != 0U ||
       g_usb_rescue_state == APP_W800_USB_RESCUE_APPLYING)
    {
        (void)tx_mutex_put(&g_usb_credentials_mutex);
        return APP_W800_CREDENTIALS_BUSY;
    }

    (void)strncpy(g_usb_ssid, ssid, sizeof(g_usb_ssid) - 1U);
    g_usb_ssid[sizeof(g_usb_ssid) - 1U] = '\0';
    (void)strncpy(g_usb_password, password, sizeof(g_usb_password) - 1U);
    g_usb_password[sizeof(g_usb_password) - 1U] = '\0';
    g_usb_credentials_pending = 1U;
    g_usb_rescue_state = APP_W800_USB_RESCUE_PENDING;
    (void)tx_mutex_put(&g_usb_credentials_mutex);
    return APP_W800_CREDENTIALS_ACCEPTED;
}

const char *app_w800_mqtt_host(void)
{
    return APP_W800_MQTT_HOST;
}

uint16_t app_w800_mqtt_port(void)
{
    return APP_W800_MQTT_PORT;
}
