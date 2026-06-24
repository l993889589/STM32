#include "app_board_io.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if defined(__ARMCC_VERSION)
__asm(".global __use_no_semihosting\n");
#endif

#include "app_config.h"
#include "app_ldc_config.h"
#include "app_nearlink.h"
#include "app_shell.h"
#include "at_module.h"
#include "at_module_w800.h"
#include "at_session.h"
#include "bsp.h"
#include "ldc_core.h"
#include "ldc/ldc_endpoint_threadx.h"
#include "modbus.h"
#include "mqtt_packet.h"
#include "ota_layout.h"
#include "usb_console.h"
#include "usb_vendor_transport.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND       1000U
#endif

static bool g_initialized;

static ldc_t g_usb_ldc;
static uint8_t g_usb_ldc_ring[APP_USB_RX_BUF_SIZE];
static ldc_packet_t g_usb_packets[APP_USB_PACKET_COUNT];

static ldc_endpoint_t g_rs485_endpoint;
static uint8_t g_rs485_ldc_ring[APP_RS485_RX_BUF_SIZE + 1U];
static ldc_packet_t g_rs485_packets[APP_RS485_PACKET_COUNT];
static uint8_t g_rs485_uart_rx_buf[APP_RS485_UART_RX_BUF_SIZE];
static modbus_t g_modbus_slave;
static modbus_mapping_t g_modbus_mapping;
static uint16_t g_holding_regs[APP_RS485_MODBUS_HOLDING_COUNT];
static ldc_endpoint_t g_w800_endpoint;
static uint8_t g_w800_ldc_ring[APP_W800_RX_BUF_SIZE];
static ldc_packet_t g_w800_packets[APP_W800_PACKET_COUNT];
static ALIGN_32BYTES(uint8_t g_w800_uart_rx_buf[APP_W800_UART_RX_BUF_SIZE]);
static at_session_t g_w800_at;
static at_module_t g_w800_module;
static uint16_t g_w800_local_port = APP_W800_LOCAL_PORT_START;
static volatile uint8_t g_w800_wifi_ready;
static volatile uint8_t g_w800_mqtt_online;
static volatile uint8_t g_w800_reconnect_requested;

static uint8_t g_ota_stream_buf[APP_OTA_STREAM_BUF_SIZE];
static uint32_t g_ota_stream_len;
static uint32_t g_ota_expected_address;
static uint32_t g_ota_expected_size;
static uint32_t g_ota_received_size;
static uint16_t g_ota_expected_seq;
static uint8_t g_ota_active;
typedef UINT (*app_ota_ack_write_fn)(const uint8_t *data, uint32_t len, void *arg);
static app_ota_ack_write_fn g_ota_ack_write;
static void *g_ota_ack_write_arg;
static uint16_t g_vendor_request_sequence;
static uint32_t g_vendor_stress_frames;
static uint32_t g_vendor_stress_bytes;
static uint8_t g_usb_cdc_dtr;

static UINT app_usb_cdc_write_raw(const uint8_t *data, uint32_t len);
static void app_usb_vendor_frame(const usb_vendor_frame_t *frame, void *arg);

static UINT app_ota_cdc_ack_write(const uint8_t *data, uint32_t len, void *arg)
{
    (void)arg;
    return app_usb_cdc_write_raw(data, len);
}

static UINT app_ota_vendor_ack_write(const uint8_t *data, uint32_t len, void *arg)
{
    (void)arg;
    return usb_vendor_transport_send(USB_VENDOR_CHANNEL_OTA,
                                     USB_VENDOR_FLAG_RESPONSE,
                                     g_vendor_request_sequence,
                                     data,
                                     len);
}

typedef enum
{
    APP_W800_STATE_RESET = 0,
    APP_W800_STATE_WIFI_JOIN,
    APP_W800_STATE_MQTT_SOCKET,
    APP_W800_STATE_MQTT_CONNECT,
    APP_W800_STATE_ONLINE,
    APP_W800_STATE_MQTT_RETRY
} app_w800_state_t;

static uint32_t app_time_now_ms(void *arg)
{
    uint64_t ticks;

    (void)arg;
    ticks = tx_time_get();
    return (uint32_t)((ticks * 1000ULL) / (uint64_t)TX_TIMER_TICKS_PER_SECOND);
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

static void app_log_token_line(const char *line)
{
    char out[192];
    int used;

    if(!line)
        return;

    used = snprintf(out, sizeof(out), "%s\r\n", line);
    if(used > 0)
        (void)app_usb_cdc_write((const uint8_t *)out, (uint32_t)used);
}

static void app_w800_at_log(const char *line, void *arg)
{
    char tagged[192];

    (void)arg;
    if(!line)
        return;

    (void)snprintf(tagged, sizeof(tagged), "[w800] %s", line);
    app_log_token_line(tagged);
}

static uint16_t app_crc16_modbus(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;

    while(len-- != 0U)
    {
        crc ^= *data++;
        for(uint32_t bit = 0U; bit < 8U; bit++)
        {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
        }
    }

    return crc;
}

static uint16_t app_get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t app_get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void app_ota_send_ack(uint8_t cmd, uint16_t seq, uint8_t status)
{
    char line[40];
    int used = snprintf(line, sizeof(line), "ota ack %u %u %u\r\n",
                        (unsigned int)cmd,
                        (unsigned int)seq,
                        (unsigned int)status);

    if(used > 0)
    {
        app_ota_ack_write_fn write = g_ota_ack_write;
        if(write == NULL)
            write = app_ota_cdc_ack_write;
        (void)write((const uint8_t *)line, (uint32_t)used, g_ota_ack_write_arg);
    }
}

static uint8_t app_ota_ext_range_ok(uint32_t address, uint32_t len)
{
    if(len == 0U)
        return 0U;

    if(address >= OTA_EXT_FLASH_SIZE || len > OTA_EXT_FLASH_SIZE || address > (OTA_EXT_FLASH_SIZE - len))
        return 0U;

    if(address < OTA_EXT_DOWNLOAD_ADDR && address != OTA_EXT_MANIFEST_A_ADDR && address != OTA_EXT_MANIFEST_B_ADDR)
        return 0U;

    if(address >= OTA_EXT_DOWNLOAD_ADDR &&
       address < (OTA_EXT_DOWNLOAD_ADDR + OTA_EXT_DOWNLOAD_SIZE) &&
       len <= ((OTA_EXT_DOWNLOAD_ADDR + OTA_EXT_DOWNLOAD_SIZE) - address))
    {
        return 1U;
    }

    if((address == OTA_EXT_MANIFEST_A_ADDR || address == OTA_EXT_MANIFEST_B_ADDR) &&
       len <= OTA_EXT_SECTOR_SIZE)
    {
        return 1U;
    }

    return 0U;
}

static uint8_t app_ota_erase_range(uint32_t address, uint32_t len)
{
    const uint32_t start = address & ~(OTA_EXT_SECTOR_SIZE - 1U);
    const uint32_t end = (address + len + OTA_EXT_SECTOR_SIZE - 1U) & ~(OTA_EXT_SECTOR_SIZE - 1U);

    if(!app_ota_ext_range_ok(address, len) || end < address)
        return 0U;

    for(uint32_t pos = start; pos < end; pos += OTA_EXT_SECTOR_SIZE)
    {
        if(!gd25lq128_erase_4k(pos))
            return 0U;
    }

    return 1U;
}

static uint8_t app_ota_handle_frame(uint8_t cmd, uint16_t seq, uint32_t address, const uint8_t *payload, uint16_t len)
{
    uint8_t status = APP_OTA_STATUS_OK;

    switch(cmd)
    {
    case APP_OTA_CMD_BEGIN:
        if(len != 8U)
        {
            status = APP_OTA_STATUS_BAD_FRAME;
            break;
        }
        g_ota_expected_address = address;
        g_ota_expected_size = app_get_u32_le(payload);
        g_ota_received_size = 0U;
        g_ota_expected_seq = (uint16_t)(seq + 1U);
        g_ota_active = 1U;

        if(!app_ota_ext_range_ok(address, g_ota_expected_size) ||
           !app_ota_erase_range(address, g_ota_expected_size))
        {
            status = APP_OTA_STATUS_FLASH_ERROR;
            g_ota_active = 0U;
        }
        break;

    case APP_OTA_CMD_DATA:
        if(!g_ota_active || seq != g_ota_expected_seq || address != (g_ota_expected_address + g_ota_received_size))
        {
            status = APP_OTA_STATUS_SEQUENCE;
            break;
        }
        if(!app_ota_ext_range_ok(address, len) || (g_ota_received_size + len) > g_ota_expected_size)
        {
            status = APP_OTA_STATUS_BAD_RANGE;
            break;
        }
        if(!gd25lq128_write(address, payload, len) || !gd25lq128_read_verify(address, payload, len))
        {
            status = APP_OTA_STATUS_FLASH_ERROR;
            break;
        }
        g_ota_received_size += len;
        g_ota_expected_seq++;
        break;

    case APP_OTA_CMD_MANIFEST:
        if(len != sizeof(ota_manifest_t) || (address != OTA_EXT_MANIFEST_A_ADDR && address != OTA_EXT_MANIFEST_B_ADDR))
        {
            status = APP_OTA_STATUS_BAD_RANGE;
            break;
        }
        if(!app_ota_erase_range(address, len) ||
           !gd25lq128_write(address, payload, len) ||
           !gd25lq128_read_verify(address, payload, len))
        {
            status = APP_OTA_STATUS_FLASH_ERROR;
        }
        break;

    case APP_OTA_CMD_END:
        if(!g_ota_active || g_ota_received_size != g_ota_expected_size)
        {
            status = APP_OTA_STATUS_SEQUENCE;
            break;
        }
        g_ota_active = 0U;
        break;

    case APP_OTA_CMD_RESET:
        app_ota_send_ack(cmd, seq, APP_OTA_STATUS_OK);
        tx_thread_sleep(20U);
        NVIC_SystemReset();
        break;

    default:
        status = APP_OTA_STATUS_BAD_FRAME;
        break;
    }

    if(status != APP_OTA_STATUS_OK)
        g_ota_active = 0U;

    app_ota_send_ack(cmd, seq, status);
    return status == APP_OTA_STATUS_OK;
}

static uint8_t app_usb_ota_feed(const uint8_t *data, uint32_t len,
                                app_ota_ack_write_fn ack_write, void *ack_arg)
{
    uint8_t handled = 0U;

    if(!data || len == 0U)
        return 0U;

    g_ota_ack_write = ack_write;
    g_ota_ack_write_arg = ack_arg;

    if(g_ota_stream_len == 0U && data[0] != APP_OTA_MAGIC_0)
        return 0U;

    if(len > (sizeof(g_ota_stream_buf) - g_ota_stream_len))
    {
        g_ota_stream_len = 0U;
        app_ota_send_ack(0U, 0U, APP_OTA_STATUS_BAD_FRAME);
        return 1U;
    }

    memcpy(&g_ota_stream_buf[g_ota_stream_len], data, len);
    g_ota_stream_len += len;

    while(g_ota_stream_len >= 4U)
    {
        uint32_t offset = 0U;

        while(offset + 4U <= g_ota_stream_len &&
              !(g_ota_stream_buf[offset] == APP_OTA_MAGIC_0 &&
                g_ota_stream_buf[offset + 1U] == APP_OTA_MAGIC_1 &&
                g_ota_stream_buf[offset + 2U] == APP_OTA_MAGIC_2 &&
                g_ota_stream_buf[offset + 3U] == APP_OTA_MAGIC_3))
        {
            offset++;
        }

        if(offset > 0U)
        {
            memmove(g_ota_stream_buf, &g_ota_stream_buf[offset], g_ota_stream_len - offset);
            g_ota_stream_len -= offset;
        }

        if(g_ota_stream_len < APP_OTA_HEADER_SIZE)
            break;

        {
            const uint8_t cmd = g_ota_stream_buf[4];
            const uint16_t seq = app_get_u16_le(&g_ota_stream_buf[6]);
            const uint32_t address = app_get_u32_le(&g_ota_stream_buf[8]);
            const uint16_t frame_len = app_get_u16_le(&g_ota_stream_buf[12]);
            const uint32_t total = APP_OTA_HEADER_SIZE + (uint32_t)frame_len + 2U;

            handled = 1U;

            if(frame_len > APP_OTA_MAX_PAYLOAD || total > sizeof(g_ota_stream_buf))
            {
                app_ota_send_ack(cmd, seq, APP_OTA_STATUS_BAD_FRAME);
                g_ota_stream_len = 0U;
                return 1U;
            }

            if(g_ota_stream_len < total)
                break;

            {
                const uint16_t got_crc = app_get_u16_le(&g_ota_stream_buf[APP_OTA_HEADER_SIZE + frame_len]);
                const uint16_t calc_crc = app_crc16_modbus(g_ota_stream_buf, APP_OTA_HEADER_SIZE + frame_len);

                if(got_crc != calc_crc)
                    app_ota_send_ack(cmd, seq, APP_OTA_STATUS_BAD_CRC);
                else
                    (void)app_ota_handle_frame(cmd, seq, address, &g_ota_stream_buf[APP_OTA_HEADER_SIZE], frame_len);
            }

            memmove(g_ota_stream_buf, &g_ota_stream_buf[total], g_ota_stream_len - total);
            g_ota_stream_len -= total;
        }
    }

    return handled || g_ota_stream_len > 0U;
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

static void app_put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void app_usb_vendor_error(const usb_vendor_frame_t *frame, uint8_t error)
{
    (void)usb_vendor_transport_send(frame->channel,
                                    USB_VENDOR_FLAG_RESPONSE | USB_VENDOR_FLAG_ERROR,
                                    frame->sequence,
                                    &error,
                                    1U);
}

static void app_usb_vendor_frame(const usb_vendor_frame_t *frame, void *arg)
{
    (void)arg;

    if(frame == NULL || (frame->flags & USB_VENDOR_FLAG_RESPONSE) != 0U)
        return;

    g_vendor_request_sequence = frame->sequence;
    switch(frame->channel)
    {
    case USB_VENDOR_CHANNEL_CONTROL:
        if(frame->payload_length == 0U)
        {
            app_usb_vendor_error(frame, 1U);
        }
        else if(frame->payload[0] == 1U)
        {
            static const uint8_t pong[] = {1U, 'p', 'o', 'n', 'g'};
            (void)usb_vendor_transport_send(frame->channel,
                                            USB_VENDOR_FLAG_RESPONSE,
                                            frame->sequence,
                                            pong,
                                            sizeof(pong));
        }
        else if(frame->payload[0] == 2U)
        {
            char info[128];
            int length = snprintf(info, sizeof(info),
                                  "leduo-h563;fw=%s;channels=control,ldc,ota,stress",
                                  APP_FIRMWARE_VERSION);
            if(length > 0)
            {
                uint32_t used = ((uint32_t)length < sizeof(info)) ? (uint32_t)length : sizeof(info) - 1U;
                (void)usb_vendor_transport_send(frame->channel,
                                                USB_VENDOR_FLAG_RESPONSE,
                                                frame->sequence,
                                                (const uint8_t *)info,
                                                used);
            }
        }
        else
        {
            app_usb_vendor_error(frame, 2U);
        }
        break;

    case USB_VENDOR_CHANNEL_LDC:
        (void)usb_vendor_transport_send(frame->channel,
                                        USB_VENDOR_FLAG_RESPONSE,
                                        frame->sequence,
                                        frame->payload,
                                        frame->payload_length);
        break;

    case USB_VENDOR_CHANNEL_OTA:
        if(!app_usb_ota_feed(frame->payload,
                             frame->payload_length,
                             app_ota_vendor_ack_write,
                             NULL))
            app_usb_vendor_error(frame, 3U);
        break;

    case USB_VENDOR_CHANNEL_STRESS:
    {
        uint8_t response[12];
        g_vendor_stress_frames++;
        g_vendor_stress_bytes += frame->payload_length;
        app_put_u32_le(&response[0], g_vendor_stress_frames);
        app_put_u32_le(&response[4], g_vendor_stress_bytes);
        app_put_u32_le(&response[8], usb_vendor_crc32(frame->payload, frame->payload_length));
        (void)usb_vendor_transport_send(frame->channel,
                                        USB_VENDOR_FLAG_RESPONSE,
                                        frame->sequence,
                                        response,
                                        sizeof(response));
        break;
    }

    default:
        app_usb_vendor_error(frame, 4U);
        break;
    }
}

static int app_rs485_modbus_tx(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;

    if(!data || len == 0U)
        return 0;

    return (bsp_uart_write_wait_complete(BSP_UART_RS485, data, len, APP_RS485_TX_TIMEOUT_MS) == (int)len) ? (int)len : 0;
}

static void app_rs485_uart_rx_callback(bsp_uart_port_t port, const uint8_t *data, uint16_t len, void *arg)
{
    (void)port;
    (void)arg;

    if(data && len != 0U)
        (void)ldc_endpoint_write(&g_rs485_endpoint, data, len);
}

static void app_rs485_process_packets(void)
{
    uint8_t frame[APP_RS485_LDC_MAX_FRAME];
    int len;

    while(ldc_endpoint_packet_count(&g_rs485_endpoint) > 0U)
    {
        len = ldc_endpoint_read(&g_rs485_endpoint, frame, sizeof(frame));
        if(len <= 0)
            break;

        app_print_hex("rs485 rx", frame, (uint32_t)len);
        (void)modbus_rtu_slave_process(&g_modbus_slave, frame, (uint16_t)len);
    }
}

static int app_w800_at_tx(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;

    if(!data || len == 0U)
        return -1;

    return (bsp_uart_write(BSP_UART_W800_AT, data, len, APP_W800_TX_TIMEOUT_MS) == (int)len) ? 0 : -1;
}

static void app_w800_drain_ldc(void)
{
    uint8_t frame[256];
    int len;

    while((len = ldc_endpoint_read(&g_w800_endpoint, frame, sizeof(frame))) > 0)
        at_session_input(&g_w800_at, frame, (uint32_t)len);
}

static void app_w800_poll_at(void *arg)
{
    ULONG events;

    (void)arg;
    (void)ldc_endpoint_poll(&g_w800_endpoint, &events);

    if((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
       ldc_endpoint_packet_count(&g_w800_endpoint) > 0U)
        app_w800_drain_ldc();
}

static void app_w800_wait_input(uint32_t ms, void *arg)
{
    ULONG events;

    (void)arg;
    (void)ldc_endpoint_wait_for(&g_w800_endpoint, ms, &events);
    if((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
       ldc_endpoint_packet_count(&g_w800_endpoint) > 0U)
        app_w800_drain_ldc();
}

static void app_w800_uart_rx_callback(bsp_uart_port_t port, const uint8_t *data, uint16_t len, void *arg)
{
    (void)port;
    (void)arg;

    if(data && len != 0U)
        (void)ldc_endpoint_write(&g_w800_endpoint, data, len);
}

static bool app_w800_mqtt_connect(void)
{
    uint8_t packet[128];
    uint16_t packet_len;

    packet_len = mqtt_build_connect(packet,
                                    sizeof(packet),
                                    APP_W800_MQTT_CLIENT_ID,
                                    APP_W800_MQTT_KEEPALIVE_S);

    app_usb_log_line("w800 state: mqtt connect packet\r\n");
    if(packet_len == 0U || !at_module_send_socket(&g_w800_module, packet, packet_len))
        return false;

    if(g_w800_module.socket_id >= 0)
    {
        char cmd[40];
        (void)snprintf(cmd, sizeof(cmd), "AT+SKRCV=%d,4", g_w800_module.socket_id);
        (void)at_session_cmd_expect(&g_w800_at, cmd, "+OK", 1000U, 1U);
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

    packet_len = mqtt_build_publish(packet, sizeof(packet), APP_W800_MQTT_STATUS_TOPIC, payload);

    app_usb_log_line("w800 state: mqtt publish status\r\n");
    return packet_len != 0U && at_module_send_socket(&g_w800_module, packet, packet_len);
}

static uint16_t app_w800_next_local_port(void)
{
    uint16_t port = g_w800_local_port++;

    if(g_w800_local_port > APP_W800_LOCAL_PORT_END)
        g_w800_local_port = APP_W800_LOCAL_PORT_START;

    return port;
}

UINT app_board_io_init(void)
{
    const app_ldc_port_config_t *rs485_ldc_config;
    const app_ldc_port_config_t *w800_ldc_config;
    ldc_endpoint_config_t rs485_endpoint_config;
    ldc_endpoint_config_t w800_endpoint_config;

    if(g_initialized)
        return TX_SUCCESS;

    bsp_init();

    if(usb_console_init() != TX_SUCCESS)
        return TX_MUTEX_ERROR;
    if(usb_vendor_transport_init(app_usb_vendor_frame, NULL) != TX_SUCCESS)
        return TX_MUTEX_ERROR;
    if(app_shell_init() != 0)
        return TX_PTR_ERROR;

    ldc_init(&g_usb_ldc, g_usb_ldc_ring, sizeof(g_usb_ldc_ring), g_usb_packets, APP_USB_PACKET_COUNT);
    ldc_set_mode(&g_usb_ldc, LDC_MODE_OVERWRITE);
    app_ldc_config_apply(&g_usb_ldc, APP_LDC_PORT_USB_CDC);

    rs485_ldc_config = app_ldc_config_get(APP_LDC_PORT_RS485);
    if(!rs485_ldc_config)
        return TX_PTR_ERROR;

    rs485_endpoint_config.name = rs485_ldc_config->name;
    rs485_endpoint_config.ring_buffer = g_rs485_ldc_ring;
    rs485_endpoint_config.ring_size = sizeof(g_rs485_ldc_ring);
    rs485_endpoint_config.packet_pool = g_rs485_packets;
    rs485_endpoint_config.packet_count = APP_RS485_PACKET_COUNT;
    rs485_endpoint_config.max_frame = rs485_ldc_config->max_frame;
    rs485_endpoint_config.timeout_ms = rs485_ldc_config->timeout_ms;
    rs485_endpoint_config.delimiter = rs485_ldc_config->delimiter;
    rs485_endpoint_config.mode = LDC_MODE_OVERWRITE;
    if(ldc_endpoint_init(&g_rs485_endpoint, &rs485_endpoint_config) != TX_SUCCESS)
        return TX_START_ERROR;

    for(uint16_t i = 0U; i < APP_RS485_MODBUS_HOLDING_COUNT; i++)
        g_holding_regs[i] = i;

    modbus_mapping_init(&g_modbus_mapping,
                        NULL, 0U, 0U,
                        NULL, 0U, 0U,
                        g_holding_regs, 0U, APP_RS485_MODBUS_HOLDING_COUNT,
                        NULL, 0U, 0U);
    if(modbus_rtu_slave_init(&g_modbus_slave,
                             APP_RS485_MODBUS_UNIT_ID,
                             &g_modbus_mapping,
                             app_rs485_modbus_tx,
                             NULL) != 0)
        return TX_PTR_ERROR;

    (void)bsp_uart_register_rx_callback(BSP_UART_RS485, app_rs485_uart_rx_callback, NULL);
    (void)bsp_uart_start_rx(BSP_UART_RS485, g_rs485_uart_rx_buf, sizeof(g_rs485_uart_rx_buf));

    w800_ldc_config = app_ldc_config_get(APP_LDC_PORT_W800_AT);
    if(!w800_ldc_config)
        return TX_PTR_ERROR;

    w800_endpoint_config.name = w800_ldc_config->name;
    w800_endpoint_config.ring_buffer = g_w800_ldc_ring;
    w800_endpoint_config.ring_size = sizeof(g_w800_ldc_ring);
    w800_endpoint_config.packet_pool = g_w800_packets;
    w800_endpoint_config.packet_count = APP_W800_PACKET_COUNT;
    w800_endpoint_config.max_frame = w800_ldc_config->max_frame;
    w800_endpoint_config.timeout_ms = w800_ldc_config->timeout_ms;
    w800_endpoint_config.delimiter = w800_ldc_config->delimiter;
    w800_endpoint_config.mode = LDC_MODE_OVERWRITE;
    if(ldc_endpoint_init(&g_w800_endpoint, &w800_endpoint_config) != TX_SUCCESS)
        return TX_START_ERROR;

    at_session_init(&g_w800_at,
                    app_w800_at_tx,
                    NULL,
                    app_time_now_ms,
                    NULL,
                    app_w800_wait_input,
                    NULL);
    at_session_set_logger(&g_w800_at, app_w800_at_log, NULL);
    at_session_set_poll_callback(&g_w800_at, app_w800_poll_at, NULL);
    at_module_init(&g_w800_module, &g_w800_at, &g_at_module_w800, NULL);
    (void)bsp_uart_register_rx_callback(BSP_UART_W800_AT, app_w800_uart_rx_callback, NULL);
    (void)bsp_uart_start_rx(BSP_UART_W800_AT, g_w800_uart_rx_buf, sizeof(g_w800_uart_rx_buf));

    if(app_nearlink_init() != TX_SUCCESS)
        return TX_START_ERROR;

    g_initialized = true;
    return TX_SUCCESS;
}

void app_usb_cdc_activate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm)
{
    usb_console_activate(cdc_acm);
    g_usb_cdc_dtr = 0U;
    app_shell_disconnected();
}

void app_usb_cdc_deactivate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm)
{
    usb_console_deactivate(cdc_acm);
    g_usb_cdc_dtr = 0U;
    app_shell_disconnected();
}

void app_usb_cdc_parameter_change(UX_SLAVE_CLASS_CDC_ACM *cdc_acm)
{
    UX_SLAVE_CLASS_CDC_ACM_LINE_STATE_PARAMETER line_state = {0};

    if(cdc_acm == UX_NULL ||
       ux_device_class_cdc_acm_ioctl(cdc_acm,
                                     UX_SLAVE_CLASS_CDC_ACM_IOCTL_GET_LINE_STATE,
                                     &line_state) != UX_SUCCESS)
        return;

    if(line_state.ux_slave_class_cdc_acm_parameter_dtr != 0U)
    {
        if(g_usb_cdc_dtr == 0U)
            app_shell_connected();
        g_usb_cdc_dtr = 1U;
    }
    else
    {
        g_usb_cdc_dtr = 0U;
        app_shell_disconnected();
    }
}

UX_SLAVE_CLASS_CDC_ACM *app_usb_cdc_get(void)
{
    return usb_console_instance();
}

UINT app_usb_cdc_write(const uint8_t *data, uint32_t len)
{
    if(g_ota_active)
        return UX_SUCCESS;

    return app_usb_cdc_write_raw(data, len);
}

static UINT app_usb_cdc_write_raw(const uint8_t *data, uint32_t len)
{
    return usb_console_write(data, len);
}

void app_usb_cdc_process_rx(const uint8_t *data, uint32_t len)
{
    if(!data || len == 0U)
        return;

    if(app_usb_ota_feed(data, len, app_ota_cdc_ack_write, NULL))
        return;

    if(app_shell_accepts_input(data, (uint16_t)len))
    {
        app_shell_input(data, (uint16_t)len);
        return;
    }

    (void)ldc_write(&g_usb_ldc, data, len);
    (void)ldc_flush(&g_usb_ldc);
    app_usb_drain_ldc();
}

void app_usb_cdc_service(void)
{
    app_shell_poll();
}

void app_rs485_task_entry(ULONG thread_input)
{
    ULONG events;

    (void)thread_input;

    for(;;)
    {
        if(ldc_endpoint_wait(&g_rs485_endpoint, &events) == TX_SUCCESS &&
           (events & LDC_ENDPOINT_EVT_PACKET) != 0U)
            app_rs485_process_packets();
    }
}

void app_w800_task_entry(ULONG thread_input)
{
    app_w800_state_t state = APP_W800_STATE_RESET;
    bool wifi_ready = false;
    bool spi_nor_logged = false;
    const at_wifi_config_t wifi_cfg =
    {
        APP_W800_WIFI_SSID,
        APP_W800_WIFI_PASSWORD
    };

    (void)thread_input;

    for(;;)
    {
        g_w800_wifi_ready = wifi_ready ? 1U : 0U;
        g_w800_mqtt_online = (state == APP_W800_STATE_ONLINE) ? 1U : 0U;

        if(g_w800_reconnect_requested != 0U)
        {
            g_w800_reconnect_requested = 0U;
            state = wifi_ready ? APP_W800_STATE_MQTT_RETRY : APP_W800_STATE_WIFI_JOIN;
            g_w800_mqtt_online = 0U;
        }

        if(!spi_nor_logged && usb_console_is_connected())
        {
            bsp_spi_nor_log_id(app_usb_log_line);
            spi_nor_logged = true;
        }

        switch(state)
        {
        case APP_W800_STATE_RESET:
            (void)at_module_close_socket(&g_w800_module);
            (void)at_module_reset(&g_w800_module);
            wifi_ready = false;
            state = APP_W800_STATE_WIFI_JOIN;
            break;

        case APP_W800_STATE_WIFI_JOIN:
            if(wifi_ready || at_module_connect_network(&g_w800_module, &wifi_cfg))
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
            (void)at_module_close_socket(&g_w800_module);
            {
                const at_socket_config_t socket_cfg =
                {
                    APP_W800_MQTT_HOST,
                    APP_W800_MQTT_PORT,
                    app_w800_next_local_port()
                };

                if(at_module_open_socket(&g_w800_module, &socket_cfg))
                    state = APP_W800_STATE_MQTT_CONNECT;
                else
                {
                    app_usb_log_line("w800 error: tcp socket failed\r\n");
                    tx_thread_sleep(3000U);
                    state = APP_W800_STATE_MQTT_RETRY;
                }
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
            app_w800_poll_at(NULL);
            tx_thread_sleep(1000U);
            if(!app_w800_mqtt_publish_status("heartbeat"))
            {
                app_usb_log_line("w800 warn: mqtt heartbeat failed, reconnect\r\n");
                state = APP_W800_STATE_MQTT_RETRY;
            }
            break;

        case APP_W800_STATE_MQTT_RETRY:
            (void)at_module_close_socket(&g_w800_module);
            if(at_module_is_network_ready(&g_w800_module))
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

void app_board_get_status(app_board_status_t *status)
{
    if(status == NULL)
        return;

    status->wifi_ready = g_w800_wifi_ready;
    status->mqtt_online = g_w800_mqtt_online;
    status->ota_active = g_ota_active;
    status->w800_socket_id = g_w800_module.socket_id;
    status->ota_received = g_ota_received_size;
    status->ota_expected = g_ota_expected_size;
    status->vendor_connected = usb_vendor_transport_is_connected() ? 1U : 0U;
    usb_vendor_transport_get_parser_stats(&status->vendor_frames,
                                          &status->vendor_crc_errors,
                                          &status->vendor_length_errors,
                                          &status->vendor_discarded_bytes);
    modbus_get_stats(&g_modbus_slave, &status->modbus);
}

void app_board_request_mqtt_reconnect(void)
{
    g_w800_reconnect_requested = 1U;
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
        (void)fputc((int)buf[i], stdout);

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
