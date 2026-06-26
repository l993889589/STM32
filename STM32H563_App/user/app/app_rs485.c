#include "app_rs485.h"

#include <stdio.h>

#include "app_board_io.h"
#include "app_config.h"
#include "app_event_bridge.h"
#include "app_ldc_config.h"
#include "bsp.h"
#include "../../../shared/comm/ldc_endpoint/threadx/ldc_endpoint_threadx.h"

static ldc_endpoint_t g_endpoint;
static uint8_t g_ldc_ring[APP_RS485_RX_BUF_SIZE + 1U];
static ldc_packet_t g_packets[APP_RS485_PACKET_COUNT];
static uint8_t g_uart_rx_buffer[APP_RS485_UART_RX_BUF_SIZE];
static modbus_t g_modbus_slave;
static modbus_mapping_t g_modbus_mapping;
static uint16_t g_holding_registers[APP_RS485_MODBUS_HOLDING_COUNT];

static void app_rs485_log_frame(const uint8_t *data, uint32_t length)
{
    char line[96];
    uint32_t offset = 0U;

    while(offset < length)
    {
        int used = snprintf(line, sizeof(line), "rs485 rx %lu/%lu:",
                            (unsigned long)offset,
                            (unsigned long)length);

        for(uint32_t i = 0U;
            i < 16U && offset < length && used > 0 && (uint32_t)used < sizeof(line);
            i++, offset++)
            used += snprintf(&line[used], sizeof(line) - (uint32_t)used,
                             " %02X", data[offset]);

        if(used > 0 && (uint32_t)used < sizeof(line) - 2U)
        {
            line[used++] = '\r';
            line[used++] = '\n';
            (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)used);
        }
    }
}

static int app_rs485_modbus_tx(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    if(!data || length == 0U)
        return 0;

    return bsp_uart_write_wait_complete(BSP_UART_RS485,
                                        data,
                                        length,
                                        APP_RS485_TX_TIMEOUT_MS) == (int)length ?
           (int)length : 0;
}

static void app_rs485_uart_rx(bsp_uart_port_t port,
                              const uint8_t *data,
                              uint16_t length,
                              void *arg)
{
    (void)port;
    (void)arg;
    if(data && length != 0U)
    {
        (void)ldc_endpoint_write(&g_endpoint, data, length);
        app_event_link_activity(APP_MSG_SOURCE_RS485, length);
    }
}

static void app_rs485_process_packets(void)
{
    uint8_t frame[APP_RS485_LDC_MAX_FRAME];
    int length;

    while((length = ldc_endpoint_read(&g_endpoint, frame, sizeof(frame))) > 0)
    {
        app_event_link_frame(APP_MSG_SOURCE_RS485, (uint16_t)length);
        app_rs485_log_frame(frame, (uint32_t)length);
        (void)modbus_rtu_slave_process(&g_modbus_slave, frame, (uint16_t)length);
    }
}

UINT app_rs485_init(void)
{
    const app_ldc_port_config_t *port_config;
    ldc_endpoint_config_t endpoint_config;

    port_config = app_ldc_config_get(APP_LDC_PORT_RS485);
    if(!port_config)
        return TX_PTR_ERROR;

    endpoint_config.name = port_config->name;
    endpoint_config.ring_buffer = g_ldc_ring;
    endpoint_config.ring_size = sizeof(g_ldc_ring);
    endpoint_config.packet_pool = g_packets;
    endpoint_config.packet_count = APP_RS485_PACKET_COUNT;
    endpoint_config.max_frame = port_config->max_frame;
    endpoint_config.timeout_ms = port_config->timeout_ms;
    endpoint_config.delimiter = port_config->delimiter;
    endpoint_config.mode = LDC_MODE_OVERWRITE;
    if(ldc_endpoint_init(&g_endpoint, &endpoint_config) != TX_SUCCESS)
        return TX_START_ERROR;

    for(uint16_t i = 0U; i < APP_RS485_MODBUS_HOLDING_COUNT; i++)
        g_holding_registers[i] = i;

    modbus_mapping_init(&g_modbus_mapping,
                        NULL, 0U, 0U,
                        NULL, 0U, 0U,
                        g_holding_registers, 0U, APP_RS485_MODBUS_HOLDING_COUNT,
                        NULL, 0U, 0U);

    if(modbus_rtu_slave_init(&g_modbus_slave,
                             APP_RS485_MODBUS_UNIT_ID,
                             &g_modbus_mapping,
                             app_rs485_modbus_tx,
                             NULL) != 0)
        return TX_PTR_ERROR;

    if(bsp_uart_register_rx_callback(BSP_UART_RS485, app_rs485_uart_rx, NULL) != 0 ||
       bsp_uart_start_rx(BSP_UART_RS485, g_uart_rx_buffer, sizeof(g_uart_rx_buffer)) != 0)
        return TX_START_ERROR;

    return TX_SUCCESS;
}

void app_rs485_task_entry(ULONG thread_input)
{
    ULONG events;

    (void)thread_input;
    for(;;)
    {
        if(ldc_endpoint_wait(&g_endpoint, &events) == TX_SUCCESS &&
           (events & LDC_ENDPOINT_EVT_PACKET) != 0U)
            app_rs485_process_packets();
    }
}

void app_rs485_get_stats(modbus_stats_t *stats)
{
    if(stats)
        modbus_get_stats(&g_modbus_slave, stats);
}

bool app_rs485_get_ldc_stats(ldc_stats_t *stats)
{
    return ldc_endpoint_get_stats(&g_endpoint, stats);
}
