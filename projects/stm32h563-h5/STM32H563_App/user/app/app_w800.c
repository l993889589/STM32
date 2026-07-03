#include "app_w800.h"

#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_board_io.h"
#include "app_rs485.h"
#include "at_module.h"
#include "at_module_w800.h"
#include "at_session.h"
#include "bsp.h"
#include "ldc_easy.h"
#include "ldc_port_irq.h"
#include "mqtt_packet.h"
#include "usb_console.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

#define APP_W800_LDC_MAX_FRAME       256U
#define APP_W800_LDC_IDLE_TIMEOUT_MS 20U
#define APP_ENABLE_W800_LOG          0U
#define APP_W800_WIFI_SSID           "CU_eaJU"
#define APP_W800_WIFI_PASSWORD       "cgzrte4s"
#define APP_W800_MQTT_HOST           "192.168.1.4"
#define APP_W800_MQTT_PORT           1883U
#define APP_W800_RX_BUF_SIZE         1024U
#define APP_W800_UART_RX_BUF_SIZE    256U
#define APP_W800_PACKET_COUNT        16U
#define APP_W800_TX_TIMEOUT_MS       1000U
#define APP_W800_CMD_TIMEOUT_MS      3000U
#define APP_W800_JOIN_TIMEOUT_MS     45000U
#define APP_W800_MQTT_KEEPALIVE_S    60U
#define APP_W800_MQTT_CLIENT_ID      "leduo-h563-w800"
#define APP_W800_MQTT_STATUS_TOPIC   "leduo/w800/status"
#define APP_W800_MQTT_CONFIG_TOPIC   "leduo/w800/config"
#define APP_W800_MQTT_DATA_TOPIC     "leduo/w800/modbus/data"
#define APP_W800_MQTT_CMD_TOPIC      "leduo/w800/cmd"
#define APP_W800_LOCAL_PORT_START    18830U
#define APP_W800_LOCAL_PORT_END      18930U

typedef enum
{
    APP_W800_STATE_RESET = 0,
    APP_W800_STATE_WIFI_JOIN,
    APP_W800_STATE_MQTT_SOCKET,
    APP_W800_STATE_MQTT_CONNECT,
    APP_W800_STATE_ONLINE,
    APP_W800_STATE_MQTT_RETRY
} app_w800_state_t;

static ldc_easy_t g_ldc;
static uint8_t g_ldc_ring[APP_W800_RX_BUF_SIZE];
static ldc_packet_t g_packets[APP_W800_PACKET_COUNT];
static ALIGN_32BYTES(uint8_t g_uart_rx_buffer[APP_W800_UART_RX_BUF_SIZE]);
static TX_SEMAPHORE g_rx_sem;
static at_session_t g_at_session;
static at_module_t g_module;
static uint16_t g_local_port = APP_W800_LOCAL_PORT_START;
static volatile uint8_t g_wifi_ready;
static volatile uint8_t g_mqtt_online;
static volatile uint8_t g_reconnect_requested;
static volatile uint8_t g_publish_status_requested;
static volatile uint8_t g_publish_config_requested;
static volatile uint8_t g_publish_data_requested;
static uint16_t g_mqtt_packet_id = 1U;
static uint8_t g_input_frame[APP_W800_LDC_MAX_FRAME];
static char g_network_command[256];
static uint8_t g_mqtt_connect_packet[128];
static uint8_t g_mqtt_subscribe_packet[160];
static uint8_t g_mqtt_status_packet[384];
static char g_mqtt_status_modbus[192];
static char g_mqtt_status_payload[256];
static uint8_t g_mqtt_modbus_packet[1024];
static char g_mqtt_modbus_payload[768];

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

    if(!text || !app_w800_json_get_string(text, "cmd", cmd, sizeof(cmd)))
        return;

    if(strcmp(cmd, "status") == 0)
    {
        g_publish_status_requested = 1U;
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

static void app_w800_scan_network_command(const uint8_t *data, uint16_t length)
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
        app_w800_apply_json_command(g_network_command);
        offset = (uint16_t)((json_end < &data[length]) ?
                            (uint16_t)(json_end - data + 1U) :
                            (uint16_t)(mark - data + 5U));
    }

    mark = app_w800_find_bytes(data, length, "mbctl:");
    if(!mark)
        return;

    copy_len = (uint16_t)(length - (uint16_t)(mark - data));
    if(copy_len >= sizeof(g_network_command))
        copy_len = sizeof(g_network_command) - 1U;

    memcpy(g_network_command, mark, copy_len);
    g_network_command[copy_len] = '\0';
    for(uint16_t i = 0U; i < copy_len; i++)
    {
        if(g_network_command[i] == '\r' || g_network_command[i] == '\n' || g_network_command[i] == '"' ||
           g_network_command[i] == '}' || g_network_command[i] == ']')
        {
            g_network_command[i] = '\0';
            break;
        }
    }

    (void)app_rs485_apply_network_command(g_network_command);
    g_publish_config_requested = 1U;
    g_publish_data_requested = 1U;
}

static void app_w800_drain_input(void)
{
    int length;

    while((length = ldc_easy_pop(&g_ldc, g_input_frame, sizeof(g_input_frame))) > 0)
    {
        app_w800_scan_network_command(g_input_frame, (uint16_t)length);
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

    app_w800_log_line("w800 state: mqtt connect packet\r\n");
    if(packet_length == 0U || !at_module_send_socket(&g_module, g_mqtt_connect_packet, packet_length))
        return false;

    if(g_module.socket_id >= 0)
    {
        char command[40];
        (void)snprintf(command, sizeof(command), "AT+SKRCV=%d,4", g_module.socket_id);
        (void)at_session_cmd_expect(&g_at_session, command, "+OK", 1000U, 1U);
    }
    return true;
}

static bool app_w800_mqtt_subscribe_commands(void)
{
    uint16_t packet_length;

    packet_length = mqtt_build_subscribe(g_mqtt_subscribe_packet,
                                         sizeof(g_mqtt_subscribe_packet),
                                         g_mqtt_packet_id++,
                                         APP_W800_MQTT_CMD_TOPIC,
                                         0U);

    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_subscribe_packet, packet_length);
}

static bool app_w800_mqtt_publish_status(const char *mode)
{
    uint16_t packet_length;

    (void)app_rs485_format_network_payload(APP_RS485_NET_STATUS, g_mqtt_status_modbus, sizeof(g_mqtt_status_modbus));
    (void)snprintf(g_mqtt_status_payload, sizeof(g_mqtt_status_payload),
                   "{\"deviceId\":\"%s\",\"online\":true,\"mode\":\"%s\",\"broker\":\"%s:%u\",\"modbus\":%s}",
                   APP_W800_MQTT_CLIENT_ID,
                   mode ? mode : "boot",
                   APP_W800_MQTT_HOST,
                   (unsigned int)APP_W800_MQTT_PORT,
                   g_mqtt_status_modbus);

    packet_length = mqtt_build_publish(g_mqtt_status_packet,
                                       sizeof(g_mqtt_status_packet),
                                       APP_W800_MQTT_STATUS_TOPIC,
                                       g_mqtt_status_payload);
    app_w800_log_line("w800 state: mqtt publish status\r\n");
    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_status_packet, packet_length);
}

static bool app_w800_mqtt_publish_modbus(app_rs485_net_payload_t type, const char *topic)
{
    uint16_t packet_length;

    if(app_rs485_format_network_payload(type, g_mqtt_modbus_payload, sizeof(g_mqtt_modbus_payload)) <= 0)
        return false;

    packet_length = mqtt_build_publish(g_mqtt_modbus_packet, sizeof(g_mqtt_modbus_packet), topic, g_mqtt_modbus_payload);
    return packet_length != 0U && at_module_send_socket(&g_module, g_mqtt_modbus_packet, packet_length);
}

static void app_w800_fetch_socket_input(void)
{
    char command[40];

    if(g_module.socket_id < 0)
        return;

    (void)snprintf(command, sizeof(command), "AT+SKRCV=%d,256", g_module.socket_id);
    (void)at_session_cmd_expect(&g_at_session, command, "+OK", 200U, 1U);
    app_w800_poll_input(NULL);
}

static uint16_t app_w800_next_local_port(void)
{
    uint16_t port = g_local_port++;

    if(g_local_port > APP_W800_LOCAL_PORT_END)
        g_local_port = APP_W800_LOCAL_PORT_START;
    return port;
}

UINT app_w800_init(void)
{
    ldc_easy_config_t ldc_config;
    UINT status;

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
    ldc_config.delimiter_enabled = true;
    ldc_config.delimiter = (uint8_t)'\n';
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

void app_w800_task_entry(ULONG thread_input)
{
    app_w800_state_t state = APP_W800_STATE_RESET;
    app_w800_state_t reported_state = APP_W800_STATE_RESET;
    bool state_reported = false;
    bool wifi_ready = false;
    bool spi_nor_logged = false;
    uint32_t last_command_fetch_ms = 0U;
    const at_wifi_config_t wifi_config =
    {
        APP_W800_WIFI_SSID,
        APP_W800_WIFI_PASSWORD
    };

    (void)thread_input;
    for(;;)
    {
        g_wifi_ready = wifi_ready ? 1U : 0U;
        g_mqtt_online = state == APP_W800_STATE_ONLINE ? 1U : 0U;
        if(!state_reported || state != reported_state)
        {
            reported_state = state;
            state_reported = true;
        }

        if(g_reconnect_requested != 0U)
        {
            g_reconnect_requested = 0U;
            state = wifi_ready ? APP_W800_STATE_MQTT_RETRY : APP_W800_STATE_WIFI_JOIN;
            g_mqtt_online = 0U;
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
            state = APP_W800_STATE_WIFI_JOIN;
            break;

        case APP_W800_STATE_WIFI_JOIN:
            if(wifi_ready || at_module_connect_network(&g_module, &wifi_config))
            {
                wifi_ready = true;
                app_w800_log_line("w800 state: wifi ready\r\n");
                state = APP_W800_STATE_MQTT_SOCKET;
            }
            else
            {
                app_w800_log_line("w800 error: wifi join failed\r\n");
                tx_thread_sleep(3000U);
                state = APP_W800_STATE_RESET;
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
                (void)app_w800_mqtt_subscribe_commands();
                tx_thread_sleep(200U);
                app_w800_fetch_socket_input();
                last_command_fetch_ms = app_w800_now_ms(NULL);
                (void)app_w800_mqtt_publish_modbus(APP_RS485_NET_CONFIG, APP_W800_MQTT_CONFIG_TOPIC);
                if(app_w800_mqtt_publish_status("online"))
                    app_w800_log_line("w800 state: mqtt online\r\n");
                else
                    app_w800_log_line("w800 warn: mqtt status publish failed\r\n");
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
            app_w800_poll_input(NULL);
            if((uint32_t)(app_w800_now_ms(NULL) - last_command_fetch_ms) >= 15000U)
            {
                app_w800_fetch_socket_input();
                last_command_fetch_ms = app_w800_now_ms(NULL);
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
            tx_thread_sleep(1000U);
            if(!app_w800_mqtt_publish_status("heartbeat"))
            {
                app_w800_log_line("w800 warn: mqtt heartbeat failed, reconnect\r\n");
                state = APP_W800_STATE_MQTT_RETRY;
            }
            else
            {
                (void)app_w800_mqtt_publish_modbus(APP_RS485_NET_DATA, APP_W800_MQTT_DATA_TOPIC);
            }
            break;

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
                app_w800_log_line("w800 warn: wifi lost, rejoin\r\n");
                tx_thread_sleep(1000U);
                state = APP_W800_STATE_WIFI_JOIN;
            }
            break;

        default:
            state = APP_W800_STATE_RESET;
            break;
        }
    }
}

void app_w800_get_status(app_w800_status_t *status)
{
    if(!status)
        return;
    status->wifi_ready = g_wifi_ready;
    status->mqtt_online = g_mqtt_online;
    status->socket_id = g_module.socket_id;
    (void)ldc_easy_get_stats(&g_ldc, &status->ldc);
}

void app_w800_request_reconnect(void)
{
    g_reconnect_requested = 1U;
}

const char *app_w800_wifi_ssid(void)
{
    return APP_W800_WIFI_SSID;
}

const char *app_w800_mqtt_host(void)
{
    return APP_W800_MQTT_HOST;
}

uint16_t app_w800_mqtt_port(void)
{
    return APP_W800_MQTT_PORT;
}
