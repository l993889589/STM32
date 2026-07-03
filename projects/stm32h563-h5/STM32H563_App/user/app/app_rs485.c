/*
 * Dual RS485 / Modbus RTU master service.
 *
 * The public app_rs485_* names are kept so the existing ThreadX and board
 * status code can continue to call this module. Internally the service owns
 * two independent Modbus master ports:
 *
 * - port 0: USART2, BSP_UART_RS485_1
 * - port 1: UART4,  BSP_UART_RS485_2
 */
#include "app_rs485.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "bsp.h"
#include "ldc_easy.h"
#include "ldc_port_irq.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

#define APP_MB_PORT_COUNT                       2U
#define APP_RS485_LDC_MAX_FRAME                 256U
#define APP_RS485_MODBUS_UNIT_ID                1U
#define APP_RS485_UART_BAUDRATE                 115200U
#define APP_RS485_FRAME_GAP_US                  ((APP_RS485_UART_BAUDRATE > 19200U) ? 1750U : ((38500000U + APP_RS485_UART_BAUDRATE - 1U) / APP_RS485_UART_BAUDRATE))
#define APP_RS485_FRAME_GAP_MS                  ((APP_RS485_FRAME_GAP_US + 999U) / 1000U)
#define APP_RS485_RX_BUF_SIZE                   256U
#define APP_RS485_UART_RX_BUF_SIZE              256U
#define APP_RS485_PACKET_COUNT                  8U
#define APP_RS485_TX_TIMEOUT_MS                 100U
#define APP_MODBUS_MASTER_DEVICE_COUNT          16U
#define APP_MODBUS_MASTER_POLL_COUNT            64U
#define APP_MODBUS_MASTER_MAX_REGS              16U
#define APP_MODBUS_MASTER_RESPONSE_MS           200U
#define APP_MODBUS_MASTER_GAP_MS                20U
#define APP_MODBUS_MASTER_DEFAULT_PERIOD_MS     1000U
#define APP_RS485_DEVICE_ID                     "leduo-h563-w800"
#define APP_MB_FRAME_MAX                        APP_RS485_LDC_MAX_FRAME
#define APP_MB_COMMAND_MAX                      96U
#define APP_MB_ERROR_NONE                       0U
#define APP_MB_ERROR_DISABLED                   1U
#define APP_MB_ERROR_TX                         2U
#define APP_MB_ERROR_TIMEOUT                    3U
#define APP_MB_ERROR_CRC                        4U
#define APP_MB_ERROR_RESPONSE                   5U
#define APP_MB_ERROR_EXCEPTION                  6U

typedef struct
{
    uint8_t enabled;
    uint32_t baudrate;
    uint16_t response_timeout_ms;
    uint16_t inter_request_gap_ms;
} app_mb_port_config_t;

typedef struct
{
    uint8_t enabled;
    uint8_t port_id;
    uint8_t slave_addr;
    uint16_t poll_period_ms;
} app_mb_device_config_t;

typedef struct
{
    uint8_t enabled;
    uint8_t device_index;
    uint8_t function;
    uint16_t reg_addr;
    uint16_t reg_count;
    uint32_t next_due_ms;
} app_mb_poll_item_t;

typedef struct
{
    uint8_t valid;
    uint8_t online;
    uint8_t last_error;
    uint8_t port_id;
    uint8_t slave_addr;
    uint8_t function;
    uint16_t reg_addr;
    uint16_t reg_count;
    uint16_t values[APP_MODBUS_MASTER_MAX_REGS];
    uint32_t last_update_ms;
} app_mb_poll_result_t;

typedef struct
{
    ldc_easy_t ldc;
    uint8_t ldc_ring[APP_RS485_RX_BUF_SIZE + 1U];
    ldc_packet_t packets[APP_RS485_PACKET_COUNT];
    uint8_t uart_rx_buffer[APP_RS485_UART_RX_BUF_SIZE];
    TX_SEMAPHORE rx_sem;
    bsp_uart_port_t uart;
    const char *name;
    modbus_stats_t stats;
    ldc_stats_t ldc_stats;
    uint32_t last_accounted_us;
    uint8_t initialized;
    uint8_t last_error;
} app_mb_port_t;

static app_mb_port_t g_ports[APP_MB_PORT_COUNT] =
{
    {.uart = BSP_UART_RS485_1, .name = "rs485-uart2"},
    {.uart = BSP_UART_RS485_2, .name = "rs485-uart4"},
};

static app_mb_port_config_t g_port_config[APP_MB_PORT_COUNT] =
{
    {1U, APP_RS485_UART_BAUDRATE, APP_MODBUS_MASTER_RESPONSE_MS, APP_MODBUS_MASTER_GAP_MS},
    {0U, APP_RS485_UART_BAUDRATE, APP_MODBUS_MASTER_RESPONSE_MS, APP_MODBUS_MASTER_GAP_MS},
};

static app_mb_device_config_t g_devices[APP_MODBUS_MASTER_DEVICE_COUNT];
static app_mb_poll_item_t g_polls[APP_MODBUS_MASTER_POLL_COUNT];
static app_mb_poll_result_t g_results[APP_MODBUS_MASTER_POLL_COUNT];
static volatile uint8_t g_config_version;

static uint32_t app_rs485_now_ms(void)
{
    uint64_t ticks = tx_time_get();
    return (uint32_t)((ticks * 1000ULL) / (uint64_t)TX_TIMER_TICKS_PER_SECOND);
}

static uint32_t app_rs485_now_us(void)
{
    return bsp_dwt_get_us();
}

static void app_rs485_clear_rx_signal(app_mb_port_t *port)
{
    if(port == NULL)
        return;

    while(tx_semaphore_get(&port->rx_sem, TX_NO_WAIT) == TX_SUCCESS)
    {
    }
}

static void app_rs485_account_time(app_mb_port_t *port)
{
    uint32_t now_us;
    uint32_t elapsed_us;

    if(port == NULL)
        return;

    now_us = app_rs485_now_us();
    elapsed_us = now_us - port->last_accounted_us;
    if(elapsed_us != 0U)
    {
        port->last_accounted_us = now_us;
        ldc_easy_tick_us(&port->ldc, elapsed_us);
    }
}

static void app_rs485_ldc_event(ldc_easy_t *queue, ldc_easy_event_t event, void *arg)
{
    app_mb_port_t *port = (app_mb_port_t *)arg;

    (void)queue;
    if(port != NULL && event == LDC_EASY_EVT_PACKET)
    {
        (void)tx_semaphore_put(&port->rx_sem);
    }
}

static void app_rs485_uart_rx(bsp_uart_port_t port,
                              const uint8_t *data,
                              uint16_t length,
                              void *arg)
{
    app_mb_port_t *ctx = (app_mb_port_t *)arg;
    uint32_t written;

    (void)port;
    if(ctx && data && length != 0U)
    {
        written = ldc_easy_add(&ctx->ldc, data, length);
        if(written != 0U)
            ctx->last_accounted_us = app_rs485_now_us();
        if(written != (uint32_t)length)
        {
            (void)ldc_easy_abort(&ctx->ldc);
        }
    }
}

static void app_rs485_seed_default_poll_table(void)
{
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_polls, 0, sizeof(g_polls));
    memset(g_results, 0, sizeof(g_results));

    g_devices[0].enabled = 1U;
    g_devices[0].port_id = 0U;
    g_devices[0].slave_addr = 1U;
    g_devices[0].poll_period_ms = APP_MODBUS_MASTER_DEFAULT_PERIOD_MS;

    g_polls[0].enabled = 1U;
    g_polls[0].device_index = 0U;
    g_polls[0].function = MODBUS_FC_READ_HOLDING_REGISTERS;
    g_polls[0].reg_addr = 0U;
    g_polls[0].reg_count = 2U;
}

static uint16_t app_rs485_build_read_request(uint8_t slave,
                                             uint8_t function,
                                             uint16_t reg_addr,
                                             uint16_t reg_count,
                                             uint8_t *out,
                                             uint16_t out_size)
{
    uint16_t crc;

    if(!out || out_size < 8U ||
       slave == MODBUS_BROADCAST_ADDRESS ||
       (function != MODBUS_FC_READ_HOLDING_REGISTERS &&
        function != MODBUS_FC_READ_INPUT_REGISTERS) ||
       reg_count == 0U ||
       reg_count > APP_MODBUS_MASTER_MAX_REGS)
        return 0U;

    out[0] = slave;
    out[1] = function;
    out[2] = (uint8_t)(reg_addr >> 8);
    out[3] = (uint8_t)(reg_addr & 0xFFU);
    out[4] = (uint8_t)(reg_count >> 8);
    out[5] = (uint8_t)(reg_count & 0xFFU);
    crc = modbus_rtu_crc(out, 6U);
    out[6] = (uint8_t)(crc & 0xFFU);
    out[7] = (uint8_t)(crc >> 8);
    return 8U;
}

static uint8_t app_rs485_parse_read_response(const uint8_t *frame,
                                             uint16_t length,
                                             uint8_t slave,
                                             uint8_t function,
                                             uint16_t reg_count,
                                             uint16_t *values)
{
    uint8_t byte_count;

    if(!frame || !values || length < 5U)
        return APP_MB_ERROR_RESPONSE;

    if(!modbus_rtu_crc_valid(frame, length))
        return APP_MB_ERROR_CRC;

    if(frame[0] != slave)
        return APP_MB_ERROR_RESPONSE;

    if((frame[1] & 0x80U) != 0U)
        return APP_MB_ERROR_EXCEPTION;

    if(frame[1] != function)
        return APP_MB_ERROR_RESPONSE;

    byte_count = frame[2];
    if(byte_count != (uint8_t)(reg_count * 2U) ||
       length != (uint16_t)(3U + byte_count + 2U))
        return APP_MB_ERROR_RESPONSE;

    for(uint16_t i = 0U; i < reg_count; i++)
    {
        uint16_t pos = (uint16_t)(3U + (i * 2U));
        values[i] = (uint16_t)(((uint16_t)frame[pos] << 8) | frame[pos + 1U]);
    }

    return APP_MB_ERROR_NONE;
}

static int app_rs485_read_response(app_mb_port_t *port,
                                   uint8_t *frame,
                                   uint16_t frame_size,
                                   uint16_t timeout_ms)
{
    uint32_t start = app_rs485_now_ms();
    int length;

    for(;;)
    {
        app_rs485_account_time(port);
        while((length = ldc_easy_pop(&port->ldc, frame, frame_size)) > 0)
        {
            return length;
        }

        if((uint32_t)(app_rs485_now_ms() - start) >= timeout_ms)
            break;

        (void)tx_semaphore_get(&port->rx_sem, 1U);
    }

    app_rs485_account_time(port);
    while((length = ldc_easy_pop(&port->ldc, frame, frame_size)) > 0)
    {
        return length;
    }

    return 0;
}

static void app_rs485_poll_one(uint16_t poll_index)
{
    app_mb_poll_item_t *poll;
    app_mb_device_config_t *device;
    app_mb_port_t *port;
    app_mb_poll_result_t *result;
    uint8_t request[8];
    uint8_t response[APP_MB_FRAME_MAX];
    uint16_t request_len;
    int response_len;
    uint8_t parse_error;

    if(poll_index >= APP_MODBUS_MASTER_POLL_COUNT)
        return;

    poll = &g_polls[poll_index];
    result = &g_results[poll_index];
    if(!poll->enabled || poll->device_index >= APP_MODBUS_MASTER_DEVICE_COUNT)
        return;

    device = &g_devices[poll->device_index];
    if(!device->enabled || device->port_id >= APP_MB_PORT_COUNT)
        return;

    port = &g_ports[device->port_id];
    result->valid = 1U;
    result->port_id = device->port_id;
    result->slave_addr = device->slave_addr;
    result->function = poll->function;
    result->reg_addr = poll->reg_addr;
    result->reg_count = poll->reg_count;

    if(!g_port_config[device->port_id].enabled || !port->initialized)
    {
        result->online = 0U;
        result->last_error = APP_MB_ERROR_DISABLED;
        port->last_error = APP_MB_ERROR_DISABLED;
        return;
    }

    request_len = app_rs485_build_read_request(device->slave_addr,
                                               poll->function,
                                               poll->reg_addr,
                                               poll->reg_count,
                                               request,
                                               sizeof(request));
    if(request_len == 0U)
    {
        result->online = 0U;
        result->last_error = APP_MB_ERROR_RESPONSE;
        return;
    }

    app_rs485_account_time(port);
    while(ldc_easy_pop(&port->ldc, response, sizeof(response)) > 0)
    {
        ;
    }
    app_rs485_clear_rx_signal(port);
    (void)ldc_easy_abort(&port->ldc);

    if(bsp_uart_write_wait_complete(port->uart,
                                    request,
                                    request_len,
                                    APP_RS485_TX_TIMEOUT_MS) != (int)request_len)
    {
        port->stats.transport_errors++;
        result->online = 0U;
        result->last_error = APP_MB_ERROR_TX;
        port->last_error = APP_MB_ERROR_TX;
        return;
    }
    port->stats.tx_frames++;

    response_len = app_rs485_read_response(port,
                                           response,
                                           sizeof(response),
                                           g_port_config[device->port_id].response_timeout_ms);
    if(response_len <= 0)
    {
        port->stats.transport_errors++;
        result->online = 0U;
        result->last_error = APP_MB_ERROR_TIMEOUT;
        port->last_error = APP_MB_ERROR_TIMEOUT;
        return;
    }
    port->stats.rx_frames++;

    parse_error = app_rs485_parse_read_response(response,
                                                (uint16_t)response_len,
                                                device->slave_addr,
                                                poll->function,
                                                poll->reg_count,
                                                result->values);
    if(parse_error != APP_MB_ERROR_NONE)
    {
        if(parse_error == APP_MB_ERROR_CRC)
            port->stats.crc_errors++;
        else if(parse_error == APP_MB_ERROR_EXCEPTION)
            port->stats.exceptions++;
        else
            port->stats.ignored_frames++;

        result->online = 0U;
        result->last_error = parse_error;
        port->last_error = parse_error;
        return;
    }

    result->online = 1U;
    result->last_error = APP_MB_ERROR_NONE;
    result->last_update_ms = app_rs485_now_ms();
    port->last_error = APP_MB_ERROR_NONE;
}

UINT app_rs485_init(void)
{
    ldc_easy_config_t ldc_config;
    UINT status;

    app_rs485_seed_default_poll_table();

    for(uint32_t i = 0U; i < APP_MB_PORT_COUNT; i++)
    {
        app_mb_port_t *port = &g_ports[i];

        status = tx_semaphore_create(&port->rx_sem, (CHAR *)port->name, 0U);
        if(status != TX_SUCCESS)
            return status;

        memset(&ldc_config, 0, sizeof(ldc_config));
        ldc_config.ring_buffer = port->ldc_ring;
        ldc_config.ring_size = sizeof(port->ldc_ring);
        ldc_config.packet_pool = port->packets;
        ldc_config.packet_count = APP_RS485_PACKET_COUNT;
        ldc_config.max_frame = APP_RS485_LDC_MAX_FRAME;
        ldc_config.timeout_ms = APP_RS485_FRAME_GAP_MS;
        ldc_config.timeout_us = APP_RS485_FRAME_GAP_US;
        ldc_config.mode = LDC_MODE_OVERWRITE;
        ldc_config.lock = ldc_port_irq_lock;
        ldc_config.unlock = ldc_port_irq_unlock;
        ldc_config.event_cb = app_rs485_ldc_event;
        ldc_config.event_arg = port;
        if(!ldc_easy_init(&port->ldc, &ldc_config))
            return TX_START_ERROR;

        port->last_accounted_us = app_rs485_now_us();

        if(bsp_uart_register_rx_callback(port->uart, app_rs485_uart_rx, port) != 0 ||
           bsp_uart_start_rx(port->uart, port->uart_rx_buffer, sizeof(port->uart_rx_buffer)) != 0)
            return TX_START_ERROR;

        port->initialized = 1U;
    }

    return TX_SUCCESS;
}

void app_rs485_task_entry(ULONG thread_input)
{
    uint32_t now;
    uint8_t seen_version = g_config_version;

    (void)thread_input;
    for(;;)
    {
        now = app_rs485_now_ms();

        if(seen_version != g_config_version)
        {
            seen_version = g_config_version;
            for(uint32_t i = 0U; i < APP_MODBUS_MASTER_POLL_COUNT; i++)
                g_polls[i].next_due_ms = now;
        }

        for(uint16_t i = 0U; i < APP_MODBUS_MASTER_POLL_COUNT; i++)
        {
            app_mb_poll_item_t *poll = &g_polls[i];
            app_mb_device_config_t *device;

            if(!poll->enabled || poll->device_index >= APP_MODBUS_MASTER_DEVICE_COUNT)
                continue;

            device = &g_devices[poll->device_index];
            if(!device->enabled)
                continue;

            if((int32_t)(now - poll->next_due_ms) >= 0)
            {
                app_rs485_poll_one(i);
                poll->next_due_ms = app_rs485_now_ms() + device->poll_period_ms;
                tx_thread_sleep(g_port_config[device->port_id].inter_request_gap_ms);
            }
        }

        tx_thread_sleep(10U);
    }
}

void app_rs485_get_stats(modbus_stats_t *stats)
{
    if(!stats)
        return;

    memset(stats, 0, sizeof(*stats));
    for(uint32_t i = 0U; i < APP_MB_PORT_COUNT; i++)
    {
        stats->rx_frames += g_ports[i].stats.rx_frames;
        stats->tx_frames += g_ports[i].stats.tx_frames;
        stats->ignored_frames += g_ports[i].stats.ignored_frames;
        stats->crc_errors += g_ports[i].stats.crc_errors;
        stats->exceptions += g_ports[i].stats.exceptions;
        stats->transport_errors += g_ports[i].stats.transport_errors;
    }
}

bool app_rs485_get_ldc_stats(ldc_stats_t *stats)
{
    if(!stats)
        return false;

    memset(stats, 0, sizeof(*stats));
    for(uint32_t i = 0U; i < APP_MB_PORT_COUNT; i++)
    {
        ldc_stats_t one;
        if(ldc_easy_get_stats(&g_ports[i].ldc, &one))
        {
            stats->rx_bytes += one.rx_bytes;
            stats->packets += one.packets;
            stats->overflow += one.overflow;
            stats->drop += one.drop;
            stats->overwrite_count += one.overwrite_count;
            if(one.max_used > stats->max_used)
                stats->max_used = one.max_used;
            stats->cur_used += one.cur_used;
            stats->packet_used += one.packet_used;
            if(one.packet_peak > stats->packet_peak)
                stats->packet_peak = one.packet_peak;
        }
    }
    return true;
}

uint8_t app_rs485_unit_id(void)
{
    return APP_RS485_MODBUS_UNIT_ID;
}

static const char *app_rs485_error_name(uint8_t error)
{
    switch(error)
    {
    case APP_MB_ERROR_NONE: return "ok";
    case APP_MB_ERROR_DISABLED: return "disabled";
    case APP_MB_ERROR_TX: return "tx";
    case APP_MB_ERROR_TIMEOUT: return "timeout";
    case APP_MB_ERROR_CRC: return "crc";
    case APP_MB_ERROR_RESPONSE: return "response";
    case APP_MB_ERROR_EXCEPTION: return "exception";
    default: return "unknown";
    }
}

static const char *app_rs485_ports_mode(void)
{
    if(g_port_config[0].enabled && g_port_config[1].enabled)
        return "both";
    if(g_port_config[0].enabled)
        return "2";
    if(g_port_config[1].enabled)
        return "4";
    return "off";
}

static int app_rs485_append(char *out, uint16_t out_size, int used, const char *fmt, ...)
{
    va_list args;
    int n;

    if(!out || used < 0 || (uint16_t)used >= out_size)
        return used;

    va_start(args, fmt);
    n = vsnprintf(&out[used], out_size - (uint16_t)used, fmt, args);
    va_end(args);
    if(n < 0)
        return used;
    used += n;
    if((uint16_t)used >= out_size)
        out[out_size - 1U] = '\0';
    return used;
}

int app_rs485_format_network_payload(app_rs485_net_payload_t type, char *out, uint16_t out_size)
{
    int used = 0;

    if(!out || out_size == 0U)
        return 0;

    switch(type)
    {
    case APP_RS485_NET_STATUS:
        used = snprintf(out, out_size,
                        "{\"ports\":[{\"id\":2,\"en\":%u,\"rx\":%lu,\"tx\":%lu,\"err\":\"%s\"},"
                        "{\"id\":4,\"en\":%u,\"rx\":%lu,\"tx\":%lu,\"err\":\"%s\"}],\"cfg\":%u}",
                        g_port_config[0].enabled,
                        (unsigned long)g_ports[0].stats.rx_frames,
                        (unsigned long)g_ports[0].stats.tx_frames,
                        app_rs485_error_name(g_ports[0].last_error),
                        g_port_config[1].enabled,
                        (unsigned long)g_ports[1].stats.rx_frames,
                        (unsigned long)g_ports[1].stats.tx_frames,
                        app_rs485_error_name(g_ports[1].last_error),
                        g_config_version);
        break;

    case APP_RS485_NET_CONFIG:
    {
        bool first = true;
        used = snprintf(out, out_size,
                        "{\"deviceId\":\"%s\",\"ports\":\"%s\",\"polls\":[",
                        APP_RS485_DEVICE_ID,
                        app_rs485_ports_mode());
        for(uint32_t i = 0U; i < APP_MODBUS_MASTER_POLL_COUNT; i++)
        {
            app_mb_poll_item_t *p = &g_polls[i];
            app_mb_device_config_t *d;
            if(!p->enabled || p->device_index >= APP_MODBUS_MASTER_DEVICE_COUNT)
                continue;
            d = &g_devices[p->device_index];
            used = app_rs485_append(out, out_size, used,
                                    "%s{\"port\":%u,\"enabled\":%s,\"slave\":%u,"
                                    "\"fc\":%u,\"addr\":%u,\"count\":%u,\"period\":%u}",
                                    first ? "" : ",",
                                    d->port_id == 0U ? 2U : 4U,
                                    d->enabled ? "true" : "false",
                                    d->slave_addr,
                                    p->function,
                                    p->reg_addr,
                                    p->reg_count,
                                    d->poll_period_ms);
            first = false;
        }
        used = app_rs485_append(out, out_size, used,
                                "],\"saved\":false,\"version\":%u}",
                                g_config_version);
        break;
    }

    case APP_RS485_NET_DATA:
    default:
    {
        bool first = true;
        used = snprintf(out, out_size,
                        "{\"deviceId\":\"%s\",\"samples\":[",
                        APP_RS485_DEVICE_ID);
        for(uint32_t ri = 0U; ri < APP_MODBUS_MASTER_POLL_COUNT; ri++)
        {
            const app_mb_poll_result_t *r = &g_results[ri];
            if(!r->valid)
                continue;
            used = app_rs485_append(out, out_size, used,
                                    "%s{\"port\":%u,\"slave\":%u,\"fc\":%u,"
                                    "\"addr\":%u,\"count\":%u,\"ok\":%s,"
                                    "\"err\":\"%s\",\"age\":%lu,\"values\":[",
                                    first ? "" : ",",
                                    r->port_id == 0U ? 2U : 4U,
                                    r->slave_addr,
                                    r->function,
                                    r->reg_addr,
                                    r->reg_count,
                                    r->online ? "true" : "false",
                                    app_rs485_error_name(r->last_error),
                                    (unsigned long)(app_rs485_now_ms() - r->last_update_ms));
            for(uint16_t i = 0U; i < r->reg_count && i < APP_MODBUS_MASTER_MAX_REGS; i++)
            {
                used = app_rs485_append(out, out_size, used,
                                        "%s%u",
                                        i == 0U ? "" : ",",
                                        r->values[i]);
            }
            used = app_rs485_append(out, out_size, used, "]}");
            first = false;
        }
        used = app_rs485_append(out, out_size, used, "]}");
        break;
    }
    }

    if(used < 0)
        return 0;
    if((uint16_t)used >= out_size)
        out[out_size - 1U] = '\0';
    return used;
}

static bool app_rs485_apply_ports_value(const char *value)
{
    if(strcmp(value, "2") == 0 || strcmp(value, "uart2") == 0)
    {
        g_port_config[0].enabled = 1U;
        g_port_config[1].enabled = 0U;
    }
    else if(strcmp(value, "4") == 0 || strcmp(value, "uart4") == 0)
    {
        g_port_config[0].enabled = 0U;
        g_port_config[1].enabled = 1U;
    }
    else if(strcmp(value, "both") == 0)
    {
        g_port_config[0].enabled = 1U;
        g_port_config[1].enabled = 1U;
    }
    else if(strcmp(value, "off") == 0 || strcmp(value, "none") == 0)
    {
        g_port_config[0].enabled = 0U;
        g_port_config[1].enabled = 0U;
    }
    else
    {
        return false;
    }

    g_config_version++;
    return true;
}

static char *app_rs485_next_field(char **cursor)
{
    char *start;
    char *comma;

    if(!cursor || !*cursor)
        return NULL;

    start = *cursor;
    comma = strchr(start, ',');
    if(comma)
    {
        *comma = '\0';
        *cursor = comma + 1;
    }
    else
    {
        *cursor = NULL;
    }

    return start;
}

static bool app_rs485_apply_poll_value(const char *value)
{
    char copy[APP_MB_COMMAND_MAX];
    char *fields[6];
    char *cursor;
    uint32_t numbers[6];

    if(!value || strlen(value) >= sizeof(copy))
        return false;

    strcpy(copy, value);
    cursor = copy;
    for(uint32_t i = 0U; i < 6U; i++)
    {
        fields[i] = app_rs485_next_field(&cursor);
        if(!fields[i])
            return false;
        numbers[i] = (uint32_t)strtoul(fields[i], NULL, 0);
    }

    if((numbers[0] != 2U && numbers[0] != 4U) ||
       numbers[1] == 0U || numbers[1] > 247U ||
       (numbers[2] != MODBUS_FC_READ_HOLDING_REGISTERS &&
        numbers[2] != MODBUS_FC_READ_INPUT_REGISTERS) ||
       numbers[4] == 0U || numbers[4] > APP_MODBUS_MASTER_MAX_REGS ||
       numbers[5] < 100U)
        return false;

    uint8_t slot = numbers[0] == 2U ? 0U : 1U;

    memset(&g_results[slot], 0, sizeof(g_results[slot]));

    g_devices[slot].enabled = 1U;
    g_devices[slot].port_id = slot;
    g_devices[slot].slave_addr = (uint8_t)numbers[1];
    g_devices[slot].poll_period_ms = (uint16_t)numbers[5];

    g_polls[slot].enabled = 1U;
    g_polls[slot].device_index = slot;
    g_polls[slot].function = (uint8_t)numbers[2];
    g_polls[slot].reg_addr = (uint16_t)numbers[3];
    g_polls[slot].reg_count = (uint16_t)numbers[4];
    g_polls[slot].next_due_ms = app_rs485_now_ms();

    g_config_version++;
    return true;
}

bool app_rs485_apply_network_command(const char *command)
{
    const char *payload;

    if(!command)
        return false;

    payload = strstr(command, "mbctl:");
    if(payload)
        payload += 6;
    else
        payload = command;

    if(strncmp(payload, "ports=", 6) == 0)
        return app_rs485_apply_ports_value(payload + 6);

    if(strncmp(payload, "poll=", 5) == 0)
        return app_rs485_apply_poll_value(payload + 5);

    return false;
}
