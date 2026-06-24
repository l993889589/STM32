#include "app_w800.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_board_io.h"
#include "app_config.h"
#include "app_ldc_config.h"
#include "at_module.h"
#include "at_module_w800.h"
#include "at_session.h"
#include "bsp.h"
#include "ldc/ldc_endpoint_threadx.h"
#include "mqtt_packet.h"
#include "usb_console.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

typedef enum
{
    APP_W800_STATE_RESET = 0,
    APP_W800_STATE_WIFI_JOIN,
    APP_W800_STATE_MQTT_SOCKET,
    APP_W800_STATE_MQTT_CONNECT,
    APP_W800_STATE_ONLINE,
    APP_W800_STATE_MQTT_RETRY
} app_w800_state_t;

static ldc_endpoint_t g_endpoint;
static uint8_t g_ldc_ring[APP_W800_RX_BUF_SIZE];
static ldc_packet_t g_packets[APP_W800_PACKET_COUNT];
static ALIGN_32BYTES(uint8_t g_uart_rx_buffer[APP_W800_UART_RX_BUF_SIZE]);
static at_session_t g_at_session;
static at_module_t g_module;
static uint16_t g_local_port = APP_W800_LOCAL_PORT_START;
static volatile uint8_t g_wifi_ready;
static volatile uint8_t g_mqtt_online;
static volatile uint8_t g_reconnect_requested;

static void app_w800_log_line(const char *line)
{
    if(line)
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
}

static void app_w800_log_token(const char *line)
{
    char output[192];
    int length;

    if(!line)
        return;
    length = snprintf(output, sizeof(output), "%s\r\n", line);
    if(length > 0)
        (void)app_usb_cdc_write((const uint8_t *)output, (uint32_t)length);
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

static void app_w800_drain_input(void)
{
    uint8_t frame[256];
    int length;

    while((length = ldc_endpoint_read(&g_endpoint, frame, sizeof(frame))) > 0)
        at_session_input(&g_at_session, frame, (uint32_t)length);
}

static void app_w800_poll_input(void *arg)
{
    ULONG events;

    (void)arg;
    (void)ldc_endpoint_poll(&g_endpoint, &events);
    if((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
       ldc_endpoint_packet_count(&g_endpoint) != 0U)
        app_w800_drain_input();
}

static void app_w800_wait_input(uint32_t milliseconds, void *arg)
{
    ULONG events;

    (void)arg;
    (void)ldc_endpoint_wait_for(&g_endpoint, milliseconds, &events);
    if((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
       ldc_endpoint_packet_count(&g_endpoint) != 0U)
        app_w800_drain_input();
}

static void app_w800_uart_rx(bsp_uart_port_t port,
                             const uint8_t *data,
                             uint16_t length,
                             void *arg)
{
    (void)port;
    (void)arg;
    if(data && length != 0U)
        (void)ldc_endpoint_write(&g_endpoint, data, length);
}

static bool app_w800_mqtt_connect(void)
{
    uint8_t packet[128];
    uint16_t packet_length;

    packet_length = mqtt_build_connect(packet,
                                       sizeof(packet),
                                       APP_W800_MQTT_CLIENT_ID,
                                       APP_W800_MQTT_KEEPALIVE_S);

    app_w800_log_line("w800 state: mqtt connect packet\r\n");
    if(packet_length == 0U || !at_module_send_socket(&g_module, packet, packet_length))
        return false;

    if(g_module.socket_id >= 0)
    {
        char command[40];
        (void)snprintf(command, sizeof(command), "AT+SKRCV=%d,4", g_module.socket_id);
        (void)at_session_cmd_expect(&g_at_session, command, "+OK", 1000U, 1U);
    }
    return true;
}

static bool app_w800_mqtt_publish_status(const char *mode)
{
    uint8_t packet[256];
    char payload[128];
    uint16_t packet_length;

    (void)snprintf(payload, sizeof(payload),
                   "{\"deviceId\":\"%s\",\"online\":true,\"mode\":\"%s\",\"broker\":\"%s:%u\"}",
                   APP_W800_MQTT_CLIENT_ID,
                   mode ? mode : "boot",
                   APP_W800_MQTT_HOST,
                   (unsigned int)APP_W800_MQTT_PORT);

    packet_length = mqtt_build_publish(packet,
                                       sizeof(packet),
                                       APP_W800_MQTT_STATUS_TOPIC,
                                       payload);
    app_w800_log_line("w800 state: mqtt publish status\r\n");
    return packet_length != 0U && at_module_send_socket(&g_module, packet, packet_length);
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
    const app_ldc_port_config_t *port_config;
    ldc_endpoint_config_t endpoint_config;

    port_config = app_ldc_config_get(APP_LDC_PORT_W800_AT);
    if(!port_config)
        return TX_PTR_ERROR;

    endpoint_config.name = port_config->name;
    endpoint_config.ring_buffer = g_ldc_ring;
    endpoint_config.ring_size = sizeof(g_ldc_ring);
    endpoint_config.packet_pool = g_packets;
    endpoint_config.packet_count = APP_W800_PACKET_COUNT;
    endpoint_config.max_frame = port_config->max_frame;
    endpoint_config.timeout_ms = port_config->timeout_ms;
    endpoint_config.delimiter = port_config->delimiter;
    endpoint_config.mode = LDC_MODE_OVERWRITE;
    if(ldc_endpoint_init(&g_endpoint, &endpoint_config) != TX_SUCCESS)
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
    bool wifi_ready = false;
    bool spi_nor_logged = false;
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

        if(g_reconnect_requested != 0U)
        {
            g_reconnect_requested = 0U;
            state = wifi_ready ? APP_W800_STATE_MQTT_RETRY : APP_W800_STATE_WIFI_JOIN;
            g_mqtt_online = 0U;
        }

        if(!spi_nor_logged && usb_console_is_connected())
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
            tx_thread_sleep(1000U);
            if(!app_w800_mqtt_publish_status("heartbeat"))
            {
                app_w800_log_line("w800 warn: mqtt heartbeat failed, reconnect\r\n");
                state = APP_W800_STATE_MQTT_RETRY;
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
}

void app_w800_request_reconnect(void)
{
    g_reconnect_requested = 1U;
}
