#include "app_board_io.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if defined(__ARMCC_VERSION)
__asm(".global __use_no_semihosting\n");
#endif

#include "ldc_core.h"
#include "ldc_frame_policy.h"
#include "ldc_proto_dispatcher.h"
#include "dcache.h"
#include "main.h"
#include "modbus_slave.h"
#include "gd25lq128.h"

extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart1;

#define APP_USB_RX_BUF_SIZE                256U
#define APP_USB_PACKET_COUNT               8U
#define APP_RS485_RX_BUF_SIZE              256U
#define APP_RS485_UART_RX_BUF_SIZE         256U
#define APP_RS485_PACKET_COUNT             8U
#define APP_RS485_LDC_TICK_MS              1U
#define APP_RS485_TX_TIMEOUT_MS            100U
#define APP_W800_RX_BUF_SIZE               1024U
#define APP_W800_UART_RX_BUF_SIZE          256U
#define APP_W800_PACKET_COUNT              16U
#define APP_W800_LDC_TICK_MS               1U
#define APP_W800_TX_TIMEOUT_MS             1000U
#define APP_W800_CMD_TIMEOUT_MS            3000U
#define APP_W800_JOIN_TIMEOUT_MS           45000U
#define APP_W800_MQTT_KEEPALIVE_S          60U
#define APP_W800_MQTT_CLIENT_ID            "leduo-h563-w800"
#define APP_W800_MQTT_STATUS_TOPIC         "leduo/w800/status"
#define APP_W800_RESET_PORT                GPIOC
#define APP_W800_RESET_PIN                 GPIO_PIN_9
#define APP_USB_TX_MUTEX_WAIT_TICKS        10U
#define APP_LED_TOGGLE_TICKS               1000U

static TX_MUTEX g_usb_tx_mutex;
static UX_SLAVE_CLASS_CDC_ACM *g_cdc_acm;
static bool g_initialized;

static ldc_t g_usb_ldc;
static uint8_t g_usb_ldc_ring[APP_USB_RX_BUF_SIZE];
static ldc_packet_t g_usb_packets[APP_USB_PACKET_COUNT];

static ldc_t g_rs485_ldc;
static uint8_t g_rs485_ldc_ring[APP_RS485_RX_BUF_SIZE];
static ldc_packet_t g_rs485_packets[APP_RS485_PACKET_COUNT];
static uint8_t g_rs485_uart_rx_buf[APP_RS485_UART_RX_BUF_SIZE];
static ldc_proto_dispatcher_t g_rs485_dispatcher;
static ldc_proto_entry_t g_rs485_handlers[1];
static modbus_slave_t g_modbus_slave;
static uint16_t g_holding_regs[APP_RS485_MODBUS_HOLDING_COUNT];
static volatile uint8_t g_rs485_packet_pending;

static ldc_t g_w800_ldc;
static uint8_t g_w800_ldc_ring[APP_W800_RX_BUF_SIZE];
static ldc_packet_t g_w800_packets[APP_W800_PACKET_COUNT];
static ALIGN_32BYTES(uint8_t g_w800_uart_rx_buf[APP_W800_UART_RX_BUF_SIZE]);
static volatile uint8_t g_w800_packet_pending;
static char g_w800_capture[1024];
static uint16_t g_w800_capture_len;
static int g_w800_socket = -1;
static uint16_t g_w800_local_port = 18830U;

typedef enum
{
    APP_W800_STATE_RESET = 0,
    APP_W800_STATE_WIFI_JOIN,
    APP_W800_STATE_MQTT_SOCKET,
    APP_W800_STATE_MQTT_CONNECT,
    APP_W800_STATE_ONLINE,
    APP_W800_STATE_MQTT_RETRY
} app_w800_state_t;

static uint32_t app_rs485_ldc_irq_lock(void *arg)
{
    uint32_t primask;

    (void)arg;
    primask = __get_PRIMASK();
    __disable_irq();

    return primask;
}

static void app_rs485_ldc_irq_unlock(void *arg, uint32_t state)
{
    (void)arg;

    if((state & 1U) == 0U)
        __enable_irq();
}

static void app_rs485_ldc_event_callback(void *arg, ldc_event_t evt)
{
    (void)arg;

    if(evt == LDC_EVT_PACKET)
        g_rs485_packet_pending = 1U;
}

static void app_w800_ldc_event_callback(void *arg, ldc_event_t evt)
{
    (void)arg;

    if(evt == LDC_EVT_PACKET)
        g_w800_packet_pending = 1U;
}

static void app_print_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    char line[96];
    uint32_t offset = 0U;

    while(offset < len)
    {
        int used = snprintf(line, sizeof(line), "%s %lu/%lu:",
                            prefix,
                            (unsigned long)offset,
                            (unsigned long)len);

        for(uint32_t i = 0U; i < 16U && offset < len && used > 0 && (uint32_t)used < sizeof(line); i++, offset++)
        {
            used += snprintf(&line[used], sizeof(line) - (uint32_t)used, " %02X", data[offset]);
        }

        if(used > 0 && (uint32_t)used < (sizeof(line) - 2U))
        {
            line[used++] = '\r';
            line[used++] = '\n';
            (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)used);
        }
    }
}

static void app_usb_log_line(const char *line)
{
    if(line)
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
}

static void app_spi_nor_log_id(void)
{
    gd25lq128_id_t id;
    char line[96];

    if(gd25lq128_read_id(&id))
    {
        (void)snprintf(line, sizeof(line),
                       "spi nor jedec: %02X %02X %02X\r\n",
                       id.manufacturer_id,
                       id.memory_type,
                       id.capacity);
        app_usb_log_line(line);
    }
    else
    {
        app_usb_log_line("spi nor jedec: read failed\r\n");
    }
}

static void app_w800_capture_clear(void)
{
    g_w800_capture_len = 0U;
    g_w800_capture[0] = '\0';
}

static void app_w800_capture_append(const uint8_t *data, uint32_t len)
{
    uint32_t room;

    if(!data || len == 0U)
        return;

    room = (uint32_t)(sizeof(g_w800_capture) - 1U - g_w800_capture_len);
    if(len > room)
        len = room;

    for(uint32_t i = 0U; i < len; i++)
    {
        uint8_t c = data[i];
        g_w800_capture[g_w800_capture_len++] = (char)((c >= 0x20U && c <= 0x7EU) || c == '\r' || c == '\n' ? c : '.');
    }
    g_w800_capture[g_w800_capture_len] = '\0';
}

static void app_w800_drain_ldc(uint8_t log_to_usb)
{
    uint8_t frame[256];
    int len;

    while((len = ldc_read_packet(&g_w800_ldc, frame, sizeof(frame))) > 0)
    {
        app_w800_capture_append(frame, (uint32_t)len);

        if(log_to_usb)
        {
            (void)app_usb_cdc_write((const uint8_t *)"w800 rx: ", 9U);
            (void)app_usb_cdc_write(frame, (uint32_t)len);
            (void)app_usb_cdc_write((const uint8_t *)"\r\n", 2U);
        }
    }

    g_w800_packet_pending = (ldc_packet_available(&g_w800_ldc) > 0U) ? 1U : 0U;
}

static void app_w800_uart_restart_rx(void)
{
    if(HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_w800_uart_rx_buf, sizeof(g_w800_uart_rx_buf)) == HAL_OK)
    {
        if(huart1.hdmarx)
            __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
}

static void app_w800_uart_tx(const uint8_t *data, uint16_t len)
{
    if(data && len != 0U)
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, len, APP_W800_TX_TIMEOUT_MS);
}

static void app_w800_send_cmd_no_clear(const char *cmd)
{
    char line[160];
    int used;

    if(!cmd)
        return;

    used = snprintf(line, sizeof(line), "w800 tx: %s\r\n", cmd);
    if(used > 0)
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)used);

    app_w800_uart_tx((const uint8_t *)cmd, (uint16_t)strlen(cmd));
    app_w800_uart_tx((const uint8_t *)"\r\n", 2U);
}

static void app_w800_send_cmd(const char *cmd)
{
    app_w800_capture_clear();
    app_w800_send_cmd_no_clear(cmd);
}

static bool app_w800_wait_contains(const char *token, uint32_t timeout_ms)
{
    uint32_t waited = 0U;

    while(waited < timeout_ms)
    {
        ldc_tick(&g_w800_ldc, APP_W800_LDC_TICK_MS);
        if(g_w800_packet_pending || ldc_packet_available(&g_w800_ldc) > 0U)
            app_w800_drain_ldc(1U);

        if(token && strstr(g_w800_capture, token))
            return true;

        tx_thread_sleep(1U);
        waited++;
    }

    return false;
}

static bool app_w800_cmd_expect(const char *cmd, const char *expect, uint32_t timeout_ms, uint8_t retries)
{
    for(uint8_t i = 0U; i < retries; i++)
    {
        app_w800_send_cmd(cmd);
        if(app_w800_wait_contains(expect, timeout_ms))
            return true;

        app_usb_log_line("w800 warn: command timeout\r\n");
        tx_thread_sleep(200U);
    }

    return false;
}

static int app_w800_parse_socket(void)
{
    char *ok = strstr(g_w800_capture, "+OK=");

    if(!ok)
        return -1;

    ok += 4;
    if(*ok < '0' || *ok > '9')
        return -1;

    return *ok - '0';
}

static uint32_t app_mqtt_write_remaining_length(uint8_t *out, uint32_t value)
{
    uint32_t used = 0U;

    do
    {
        uint8_t encoded = (uint8_t)(value % 128U);
        value /= 128U;
        if(value > 0U)
            encoded |= 0x80U;
        out[used++] = encoded;
    } while(value > 0U && used < 4U);

    return used;
}

static uint16_t app_mqtt_write_utf8(uint8_t *out, const char *text)
{
    uint16_t len = (uint16_t)strlen(text);

    out[0] = (uint8_t)(len >> 8);
    out[1] = (uint8_t)(len & 0xFFU);
    memcpy(&out[2], text, len);

    return (uint16_t)(len + 2U);
}

static uint16_t app_mqtt_build_connect(uint8_t *out, uint16_t max_len)
{
    const char client_id[] = APP_W800_MQTT_CLIENT_ID;
    uint32_t remaining = 10U + 2U + (uint32_t)strlen(client_id);
    uint32_t pos = 0U;

    if(max_len < (remaining + 5U))
        return 0U;

    out[pos++] = 0x10U;
    pos += app_mqtt_write_remaining_length(&out[pos], remaining);
    pos += app_mqtt_write_utf8(&out[pos], "MQTT");
    out[pos++] = 0x04U;
    out[pos++] = 0x02U;
    out[pos++] = 0x00U;
    out[pos++] = APP_W800_MQTT_KEEPALIVE_S;
    pos += app_mqtt_write_utf8(&out[pos], client_id);

    return (uint16_t)pos;
}

static uint16_t app_mqtt_build_publish(uint8_t *out, uint16_t max_len, const char *topic, const char *payload)
{
    uint32_t topic_len = (uint32_t)strlen(topic);
    uint32_t payload_len = (uint32_t)strlen(payload);
    uint32_t remaining = 2U + topic_len + payload_len;
    uint32_t pos = 0U;

    if(max_len < (remaining + 5U))
        return 0U;

    out[pos++] = 0x30U;
    pos += app_mqtt_write_remaining_length(&out[pos], remaining);
    pos += app_mqtt_write_utf8(&out[pos], topic);
    memcpy(&out[pos], payload, payload_len);
    pos += payload_len;

    return (uint16_t)pos;
}

static bool app_w800_socket_send(const uint8_t *data, uint16_t len)
{
    char cmd[64];

    if(g_w800_socket < 0 || !data || len == 0U)
        return false;

    app_w800_capture_clear();
    (void)snprintf(cmd, sizeof(cmd), "AT+SKSND=%d,%u", g_w800_socket, (unsigned int)len);
    app_w800_send_cmd_no_clear(cmd);
    tx_thread_sleep(20U);
    app_w800_uart_tx(data, len);

    return app_w800_wait_contains("+OK", APP_W800_CMD_TIMEOUT_MS);
}

static bool app_w800_mqtt_connect(void)
{
    uint8_t packet[128];
    uint16_t packet_len = app_mqtt_build_connect(packet, sizeof(packet));

    app_usb_log_line("w800 state: mqtt connect packet\r\n");
    if(packet_len == 0U || !app_w800_socket_send(packet, packet_len))
        return false;

    app_w800_capture_clear();
    if(g_w800_socket >= 0)
    {
        char cmd[40];
        (void)snprintf(cmd, sizeof(cmd), "AT+SKRCV=%d,4", g_w800_socket);
        app_w800_send_cmd_no_clear(cmd);
        (void)app_w800_wait_contains("+OK", 1000U);
    }

    return true;
}

static bool app_w800_mqtt_publish_status(const char *mode)
{
    uint8_t packet[256];
    char payload[128];
    uint16_t packet_len;

    (void)snprintf(payload, sizeof(payload),
                   "{\"deviceId\":\"%s\",\"online\":true,\"mode\":\"%s\",\"broker\":\"%s:%u\"}",
                   APP_W800_MQTT_CLIENT_ID,
                   mode ? mode : "boot",
                   APP_W800_MQTT_HOST,
                   (unsigned int)APP_W800_MQTT_PORT);

    packet_len = app_mqtt_build_publish(packet, sizeof(packet), APP_W800_MQTT_STATUS_TOPIC, payload);

    app_usb_log_line("w800 state: mqtt publish status\r\n");
    return packet_len != 0U && app_w800_socket_send(packet, packet_len);
}

static void app_w800_close_socket(void)
{
    char cmd[32];

    if(g_w800_socket < 0)
        return;

    (void)snprintf(cmd, sizeof(cmd), "AT+SKCLS=%d", g_w800_socket);
    (void)app_w800_cmd_expect(cmd, "+OK", 1000U, 1U);
    g_w800_socket = -1;
}

static bool app_w800_wifi_is_ready(void)
{
    if(!app_w800_cmd_expect("AT+LKSTT", "+OK", APP_W800_CMD_TIMEOUT_MS, 1U))
        return false;

    return strstr(g_w800_capture, "+OK=1") != NULL;
}

static bool app_w800_probe(void)
{
    app_usb_log_line("w800 state: probe\r\n");
    if(app_w800_cmd_expect("AT+", "+OK", 1000U, 5U))
        return true;

    app_usb_log_line("w800 state: probe compatible AT\r\n");
    return app_w800_cmd_expect("AT", "+OK", 1000U, 5U);
}

static bool app_w800_scan_wifi(void)
{
    bool found;

    app_usb_log_line("w800 state: scan wifi\r\n");
    app_w800_send_cmd("AT+WSCAN=3FFF,2,120");
    if(!app_w800_wait_contains("+OK", 12000U))
    {
        app_usb_log_line("w800 warn: wifi scan timeout\r\n");
        return false;
    }
    tx_thread_sleep(1000U);
    app_w800_drain_ldc(1U);

    found = strstr(g_w800_capture, APP_W800_WIFI_SSID) != NULL;
    app_usb_log_line(found ? "w800 state: configured ssid found\r\n" :
                              "w800 warn: configured ssid not found in scan\r\n");

    return found;
}

static void app_w800_reset_module(void)
{
    HAL_GPIO_WritePin(APP_W800_RESET_PORT, APP_W800_RESET_PIN, GPIO_PIN_RESET);
    tx_thread_sleep(100U);
    HAL_GPIO_WritePin(APP_W800_RESET_PORT, APP_W800_RESET_PIN, GPIO_PIN_SET);
    tx_thread_sleep(3000U);
}

static bool app_w800_wait_wifi_ready(uint32_t timeout_ms)
{
    uint32_t waited = 0U;

    while(waited < timeout_ms)
    {
        if(app_w800_wifi_is_ready())
            return true;

        tx_thread_sleep(1000U);
        waited += 1000U;
    }

    return false;
}

static bool app_w800_join_wifi(void)
{
    char cmd[96];

    if(!app_w800_probe())
        return false;

    app_usb_log_line("w800 state: config sta\r\n");
    if(!app_w800_cmd_expect("AT+WPRT=0", "+OK", APP_W800_CMD_TIMEOUT_MS, 2U))
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+SSID=\"%s\"", APP_W800_WIFI_SSID);
    if(!app_w800_cmd_expect(cmd, "+OK", APP_W800_CMD_TIMEOUT_MS, 2U))
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+KEY=1,0,\"%s\"", APP_W800_WIFI_PASSWORD);
    if(!app_w800_cmd_expect(cmd, "+OK", APP_W800_CMD_TIMEOUT_MS, 2U))
        return false;

    if(!app_w800_cmd_expect("AT+NIP=0", "+OK", APP_W800_CMD_TIMEOUT_MS, 2U))
        return false;

    app_usb_log_line("w800 state: save wifi profile\r\n");
    if(!app_w800_cmd_expect("AT+PMTF", "+OK", APP_W800_CMD_TIMEOUT_MS, 2U))
        app_usb_log_line("w800 warn: save wifi profile failed\r\n");

    app_usb_log_line("w800 state: restart after profile save\r\n");
    app_w800_send_cmd("AT+Z");
    (void)app_w800_wait_contains("+OK", 1000U);
    tx_thread_sleep(1500U);

    if(!app_w800_probe())
        return false;

    (void)app_w800_scan_wifi();

    app_usb_log_line("w800 state: join wifi\r\n");
    if(!app_w800_cmd_expect("AT+WJOIN", "+OK", APP_W800_JOIN_TIMEOUT_MS, 1U))
        return false;

    app_usb_log_line("w800 state: wait dhcp\r\n");
    return app_w800_wait_wifi_ready(15000U);
}

static bool app_w800_open_socket(void)
{
    char cmd[128];
    uint16_t local_port = g_w800_local_port++;

    if(g_w800_local_port > 18930U)
        g_w800_local_port = 18830U;

    app_usb_log_line("w800 state: tcp socket\r\n");
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+SKCT=0,0,\"%s\",%u,%u",
                   APP_W800_MQTT_HOST,
                   (unsigned int)APP_W800_MQTT_PORT,
                   (unsigned int)local_port);

    app_w800_send_cmd(cmd);
    if(!app_w800_wait_contains("+OK=", 8000U))
        return false;

    g_w800_socket = app_w800_parse_socket();
    return g_w800_socket >= 0;
}

static void app_usb_drain_ldc(void)
{
    uint8_t frame[APP_USB_LDC_MAX_FRAME];
    int len;

    while((len = ldc_read_packet(&g_usb_ldc, frame, sizeof(frame))) > 0)
    {
        static const char prefix[] = "usb rx";

        (void)app_usb_cdc_write((const uint8_t *)prefix, (uint32_t)(sizeof(prefix) - 1U));
        (void)app_usb_cdc_write((const uint8_t *)": ", 2U);
        (void)app_usb_cdc_write(frame, (uint32_t)len);
        (void)app_usb_cdc_write((const uint8_t *)"\r\n", 2U);
        app_print_hex("usb hex", frame, (uint32_t)len);
    }
}

static int app_rs485_modbus_tx(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;

    if(!data || len == 0U)
        return 0;

    if(HAL_UART_Transmit(&huart2, (uint8_t *)data, len, APP_RS485_TX_TIMEOUT_MS) != HAL_OK)
    {
        return 0;
    }

    while(__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET)
    {
    }

    return (int)len;
}

static void app_rs485_uart_restart_rx(void)
{
    (void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, g_rs485_uart_rx_buf, sizeof(g_rs485_uart_rx_buf));
}

static void app_rs485_process_packets(void)
{
    uint8_t frame[APP_RS485_LDC_MAX_FRAME];
    int len;

    if(g_rs485_packet_pending == 0U)
        return;

    while(ldc_packet_available(&g_rs485_ldc) > 0U)
    {
        len = ldc_read_packet(&g_rs485_ldc, frame, sizeof(frame));

        if(len <= 0)
            break;

        app_print_hex("rs485 rx", frame, (uint32_t)len);
        (void)ldc_proto_dispatcher_dispatch(&g_rs485_dispatcher, frame, (uint32_t)len);
    }

    g_rs485_packet_pending = (ldc_packet_available(&g_rs485_ldc) > 0U) ? 1U : 0U;
}

UINT app_board_io_init(void)
{
    ldc_frame_policy_t usb_policy;
    ldc_frame_policy_t rs485_policy;
    ldc_frame_policy_t w800_policy;

    if(g_initialized)
        return TX_SUCCESS;

    if(tx_mutex_create(&g_usb_tx_mutex, "usb cdc tx mutex", TX_INHERIT) != TX_SUCCESS)
        return TX_MUTEX_ERROR;

    ldc_init(&g_usb_ldc, g_usb_ldc_ring, sizeof(g_usb_ldc_ring), g_usb_packets, APP_USB_PACKET_COUNT);
    ldc_set_mode(&g_usb_ldc, LDC_MODE_OVERWRITE);
    usb_policy.type = LDC_FRAME_POLICY_LENGTH_TIMEOUT;
    usb_policy.max_len = APP_USB_LDC_MAX_FRAME;
    usb_policy.timeout_ms = 20U;
    usb_policy.delimiter = '\n';
    usb_policy.baudrate = 0U;
    ldc_frame_policy_apply(&g_usb_ldc, &usb_policy);

    ldc_init(&g_rs485_ldc, g_rs485_ldc_ring, sizeof(g_rs485_ldc_ring), g_rs485_packets, APP_RS485_PACKET_COUNT);
    ldc_set_lock(&g_rs485_ldc, app_rs485_ldc_irq_lock, app_rs485_ldc_irq_unlock, NULL);
    ldc_set_mode(&g_rs485_ldc, LDC_MODE_OVERWRITE);
    ldc_set_callback(&g_rs485_ldc, app_rs485_ldc_event_callback, NULL);
    rs485_policy.type = LDC_FRAME_POLICY_MODBUS_RTU;
    rs485_policy.max_len = APP_RS485_LDC_MAX_FRAME;
    rs485_policy.timeout_ms = 0U;
    rs485_policy.delimiter = -1;
    rs485_policy.baudrate = APP_RS485_UART_BAUDRATE;
    ldc_frame_policy_apply(&g_rs485_ldc, &rs485_policy);

    for(uint16_t i = 0U; i < APP_RS485_MODBUS_HOLDING_COUNT; i++)
        g_holding_regs[i] = i;

    modbus_slave_init(&g_modbus_slave,
                      APP_RS485_MODBUS_UNIT_ID,
                      g_holding_regs,
                      APP_RS485_MODBUS_HOLDING_COUNT,
                      app_rs485_modbus_tx,
                      NULL);

    ldc_proto_dispatcher_init(&g_rs485_dispatcher, g_rs485_handlers, 1U);
    (void)ldc_proto_dispatcher_register(&g_rs485_dispatcher, modbus_slave_dispatch, &g_modbus_slave);

    g_rs485_packet_pending = 0U;
    app_rs485_uart_restart_rx();

    ldc_init(&g_w800_ldc, g_w800_ldc_ring, sizeof(g_w800_ldc_ring), g_w800_packets, APP_W800_PACKET_COUNT);
    ldc_set_lock(&g_w800_ldc, app_rs485_ldc_irq_lock, app_rs485_ldc_irq_unlock, NULL);
    ldc_set_mode(&g_w800_ldc, LDC_MODE_OVERWRITE);
    ldc_set_callback(&g_w800_ldc, app_w800_ldc_event_callback, NULL);
    w800_policy.type = LDC_FRAME_POLICY_LENGTH_TIMEOUT;
    w800_policy.max_len = 256U;
    w800_policy.timeout_ms = 20U;
    w800_policy.delimiter = '\n';
    w800_policy.baudrate = APP_W800_UART_BAUDRATE;
    ldc_frame_policy_apply(&g_w800_ldc, &w800_policy);
    g_w800_packet_pending = 0U;
    app_w800_capture_clear();
    app_w800_uart_restart_rx();

    g_initialized = true;
    return TX_SUCCESS;
}

void app_usb_cdc_activate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm)
{
    g_cdc_acm = cdc_acm;
}

void app_usb_cdc_deactivate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm)
{
    if(g_cdc_acm == cdc_acm)
        g_cdc_acm = NULL;
}

UX_SLAVE_CLASS_CDC_ACM *app_usb_cdc_get(void)
{
    return g_cdc_acm;
}

UINT app_usb_cdc_write(const uint8_t *data, uint32_t len)
{
    UX_SLAVE_CLASS_CDC_ACM *cdc_acm = g_cdc_acm;
    ULONG actual = 0U;
    UINT status;

    if(!cdc_acm || !data || len == 0U)
        return UX_ERROR;

    if(tx_mutex_get(&g_usb_tx_mutex, APP_USB_TX_MUTEX_WAIT_TICKS) != TX_SUCCESS)
        return UX_ERROR;

    status = ux_device_class_cdc_acm_write(cdc_acm, (UCHAR *)data, len, &actual);
    tx_mutex_put(&g_usb_tx_mutex);

    return (status == UX_SUCCESS && actual == len) ? UX_SUCCESS : status;
}

void app_usb_cdc_process_rx(const uint8_t *data, uint32_t len)
{
    if(!data || len == 0U)
        return;

    (void)ldc_write(&g_usb_ldc, data, len);
    (void)ldc_flush(&g_usb_ldc);
    app_usb_drain_ldc();
}

void app_rs485_task_entry(ULONG thread_input)
{
    (void)thread_input;

    for(;;)
    {
        ldc_tick(&g_rs485_ldc, APP_RS485_LDC_TICK_MS);
        app_rs485_process_packets();
        tx_thread_sleep(1U);
    }
}

void app_w800_task_entry(ULONG thread_input)
{
    app_w800_state_t state = APP_W800_STATE_RESET;
    bool wifi_ready = false;
    bool spi_nor_logged = false;

    (void)thread_input;

    for(;;)
    {
        if(!spi_nor_logged && g_cdc_acm)
        {
            app_spi_nor_log_id();
            spi_nor_logged = true;
        }

        switch(state)
        {
        case APP_W800_STATE_RESET:
            app_usb_log_line("w800 state: reset\r\n");
            app_w800_close_socket();
            app_w800_reset_module();
            g_w800_socket = -1;
            wifi_ready = false;
            state = APP_W800_STATE_WIFI_JOIN;
            break;

        case APP_W800_STATE_WIFI_JOIN:
            if(wifi_ready || app_w800_join_wifi())
            {
                wifi_ready = true;
                app_usb_log_line("w800 state: wifi ready\r\n");
                state = APP_W800_STATE_MQTT_SOCKET;
            }
            else
            {
                app_usb_log_line("w800 error: wifi join failed\r\n");
                tx_thread_sleep(3000U);
                state = APP_W800_STATE_RESET;
            }
            break;

        case APP_W800_STATE_MQTT_SOCKET:
            app_w800_close_socket();
            if(app_w800_open_socket())
            {
                state = APP_W800_STATE_MQTT_CONNECT;
            }
            else
            {
                app_usb_log_line("w800 error: tcp socket failed\r\n");
                tx_thread_sleep(3000U);
                state = APP_W800_STATE_MQTT_RETRY;
            }
            break;

        case APP_W800_STATE_MQTT_CONNECT:
            if(app_w800_mqtt_connect())
            {
                if(app_w800_mqtt_publish_status("online"))
                    app_usb_log_line("w800 state: mqtt online\r\n");
                else
                    app_usb_log_line("w800 warn: mqtt status publish failed\r\n");

                state = APP_W800_STATE_ONLINE;
            }
            else
            {
                app_usb_log_line("w800 error: mqtt connect send failed\r\n");
                tx_thread_sleep(3000U);
                state = APP_W800_STATE_MQTT_RETRY;
            }
            break;

        case APP_W800_STATE_ONLINE:
            ldc_tick(&g_w800_ldc, APP_W800_LDC_TICK_MS);
            if(g_w800_packet_pending || ldc_packet_available(&g_w800_ldc) > 0U)
                app_w800_drain_ldc(1U);

            tx_thread_sleep(1000U);
            if(!app_w800_mqtt_publish_status("heartbeat"))
            {
                app_usb_log_line("w800 warn: mqtt heartbeat failed, reconnect\r\n");
                state = APP_W800_STATE_MQTT_RETRY;
            }
            break;

        case APP_W800_STATE_MQTT_RETRY:
            app_w800_close_socket();
            if(app_w800_wifi_is_ready())
            {
                wifi_ready = true;
                app_usb_log_line("w800 state: mqtt retry keep wifi\r\n");
                tx_thread_sleep(1000U);
                state = APP_W800_STATE_MQTT_SOCKET;
            }
            else
            {
                wifi_ready = false;
                app_usb_log_line("w800 warn: wifi lost, rejoin\r\n");
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

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if(huart->Instance == USART2)
    {
        if(size > sizeof(g_rs485_uart_rx_buf))
            size = sizeof(g_rs485_uart_rx_buf);

        if(size != 0U)
            (void)ldc_write(&g_rs485_ldc, g_rs485_uart_rx_buf, size);

        app_rs485_uart_restart_rx();
    }
    else if(huart->Instance == USART1)
    {
        if(size > sizeof(g_w800_uart_rx_buf))
            size = sizeof(g_w800_uart_rx_buf);

        if(size != 0U)
        {
            (void)HAL_DCACHE_InvalidateByAddr(&hdcache1,
                                               (uint32_t *)g_w800_uart_rx_buf,
                                               (uint32_t)((size + 31U) & ~31U));
            (void)ldc_write(&g_w800_ldc, g_w800_uart_rx_buf, size);
        }

        app_w800_uart_restart_rx();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if(huart->Instance == USART2)
        app_rs485_uart_restart_rx();
    else if(huart->Instance == USART1)
        app_w800_uart_restart_rx();
}

void app_led_task_entry(ULONG thread_input)
{
    (void)thread_input;

    for(;;)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_12);
				
        tx_thread_sleep(APP_LED_TOGGLE_TICKS);
    }
}

int fputc(int ch, FILE *f)
{
    uint8_t c = (uint8_t)ch;

    (void)f;
    (void)app_usb_cdc_write(&c, 1U);
    return ch;
}

typedef int FILEHANDLE;

FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)name;
    (void)openmode;
    return 0;
}

int _sys_close(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
    unsigned i;

    (void)fh;
    (void)mode;

    for(i = 0U; i < len; i++)
    {
        (void)fputc((int)buf[i], stdout);
    }

    return 0;
}

int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
    (void)fh;
    (void)buf;
    (void)mode;
    return (int)len;
}

int _sys_istty(FILEHANDLE fh)
{
    (void)fh;
    return 1;
}

int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1;
}

long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

void _ttywrch(int ch)
{
    (void)fputc(ch, stdout);
}

void _sys_exit(int return_code)
{
    (void)return_code;
    for(;;)
    {
    }
}
