#include "app_uart4_console.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_ap6212_fw_bundle.h"
#include "app_ldc_config.h"
#include "bsp.h"
#include "bsp_qspi_flash.h"

static ldc_queue_t g_uart4_console_queue;
static TX_THREAD g_uart4_console_thread;
static TX_SEMAPHORE g_uart4_console_packet_sem;
static TX_TIMER g_uart4_console_tick_timer;
static TX_MUTEX g_uart4_console_tx_mutex;

static UCHAR g_uart4_console_thread_stack[APP_UART4_ECHO_STACK_SIZE];

static uint8_t g_uart4_console_rx_dma[APP_UART4_ECHO_RX_DMA_SIZE] __ALIGNED(32);
static uint8_t g_uart4_console_ldc_ring[APP_UART4_ECHO_LDC_RING_SIZE + 1U];
static ldc_packet_t g_uart4_console_ldc_packets[APP_UART4_ECHO_LDC_PACKET_COUNT];
static uint8_t g_uart4_console_frame[APP_UART4_ECHO_LDC_MAX_FRAME];
static volatile uint8_t g_uart4_console_initialized;
static volatile uint32_t g_uart4_console_packet_signals;
static volatile uint32_t g_uart4_console_drop_events;
static volatile uint32_t g_uart4_console_overflow_events;

static void console_queue_event(ldc_queue_t *queue,
                                ldc_queue_event_t event,
                                void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_QUEUE_EVT_PACKET)
    {
        g_uart4_console_packet_signals++;
        (void)tx_semaphore_put(&g_uart4_console_packet_sem);
    }
    else if(event == LDC_QUEUE_EVT_DROP)
    {
        g_uart4_console_drop_events++;
    }
    else
    {
        g_uart4_console_overflow_events++;
    }
}

static void console_tick_timer_entry(ULONG timer_input)
{
    static uint32_t last_ms;
    uint32_t now_ms;
    uint32_t elapsed_ms;

    (void)timer_input;

    now_ms = HAL_GetTick();
    elapsed_ms = now_ms - last_ms;
    last_ms = now_ms;

    if(elapsed_ms == 0U || !ldc_queue_need_tick(&g_uart4_console_queue))
        return;

    ldc_queue_tick(&g_uart4_console_queue, elapsed_ms);
}

int app_uart4_console_write(const uint8_t *data, uint16_t length)
{
    int result;

    if(data == NULL || length == 0U || g_uart4_console_initialized == 0U)
        return -1;

    (void)tx_mutex_get(&g_uart4_console_tx_mutex, TX_WAIT_FOREVER);
    result = ldc_queue_uart_write_wait_complete(&g_uart4_console_queue,
                                                data,
                                                length,
                                                APP_SHELL_TX_TIMEOUT_MS);
    (void)tx_mutex_put(&g_uart4_console_tx_mutex);
    return result;
}

int app_uart4_console_write_string(const char *text)
{
    if(text == NULL)
        return -1;

    return app_uart4_console_write((const uint8_t *)text, (uint16_t)strlen(text));
}

int app_uart4_console_printf(const char *fmt, ...)
{
    char line[192];
    va_list args;
    int length;

    if(fmt == NULL)
        return -1;

    va_start(args, fmt);
    length = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if(length <= 0)
        return length;
    if(length >= (int)sizeof(line))
        length = (int)sizeof(line) - 1;

    return app_uart4_console_write((const uint8_t *)line, (uint16_t)length);
}

static void console_trim(uint8_t *data, uint16_t *length)
{
    uint16_t start = 0U;
    uint16_t end;

    if(data == NULL || length == NULL)
        return;

    end = *length;
    while(start < end &&
          (data[start] == ' ' || data[start] == '\r' || data[start] == '\n' || data[start] == '\t'))
    {
        start++;
    }
    while(end > start &&
          (data[end - 1U] == ' ' || data[end - 1U] == '\r' ||
           data[end - 1U] == '\n' || data[end - 1U] == '\t'))
    {
        end--;
    }

    if(start != 0U && end > start)
        memmove(data, data + start, end - start);

    *length = end - start;
    data[*length] = '\0';
}

static bool console_starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static void console_cmd_qspi_id(void)
{
    bsp_qspi_flash_id_t id;
    int status = bsp_qspi_flash_read_id(&id);

    if(status == 0)
    {
        (void)app_uart4_console_printf("[qspi] JEDEC %02X %02X %02X mode=%u capacity=%lu bytes\r\n",
                                       (unsigned int)id.manufacturer_id,
                                       (unsigned int)id.memory_type,
                                       (unsigned int)id.capacity_id,
                                       (unsigned int)id.read_mode,
                                       (unsigned long)id.capacity_bytes);
    }
    else
    {
        (void)app_uart4_console_printf("[qspi] JEDEC read failed status=%d\r\n", status);
    }
}

static void console_cmd_qspi_crc(const char *command)
{
    char *endptr;
    uint32_t address;
    uint32_t length;

    command += strlen("qspi crc");
    while(*command == ' ')
        command++;

    address = (uint32_t)strtoul(command, &endptr, 0);
    if(endptr == command)
    {
        (void)app_uart4_console_write_string("[console] usage: qspi crc <addr> <len>\r\n");
        return;
    }

    while(*endptr == ' ')
        endptr++;
    command = endptr;
    length = (uint32_t)strtoul(command, &endptr, 0);
    if(endptr == command || length == 0U)
    {
        (void)app_uart4_console_write_string("[console] usage: qspi crc <addr> <len>\r\n");
        return;
    }

    (void)app_uart4_console_printf("[qspi] crc addr=0x%08lX len=%lu value=0x%08lX\r\n",
                                   (unsigned long)address,
                                   (unsigned long)length,
                                   (unsigned long)bsp_qspi_flash_crc32(address, length));
}

static void console_cmd_qspi_fwinfo(void)
{
    app_ap6212_fw_bundle_info_t info;
    int status = app_ap6212_fw_bundle_read_info(&info);

    if(status != 0)
    {
        (void)app_uart4_console_printf("[ap6212-fw] bundle read failed status=%d\r\n",
                                       status);
        return;
    }

    (void)app_uart4_console_printf("[ap6212-fw] bundle version=%lu total=%lu header=%lu crc=%s\r\n",
                                   (unsigned long)info.version,
                                   (unsigned long)info.total_size,
                                   (unsigned long)info.header_size,
                                   app_ap6212_fw_bundle_crc_ok(&info) ? "OK" : "BAD");
    (void)app_uart4_console_printf("[ap6212-fw] fw off=%lu len=%lu crc=0x%08lX read=0x%08lX\r\n",
                                   (unsigned long)info.firmware_offset,
                                   (unsigned long)info.firmware_length,
                                   (unsigned long)info.firmware_crc32,
                                   (unsigned long)info.firmware_crc32_readback);
    (void)app_uart4_console_printf("[ap6212-fw] nvram off=%lu len=%lu crc=0x%08lX read=0x%08lX\r\n",
                                   (unsigned long)info.nvram_offset,
                                   (unsigned long)info.nvram_length,
                                   (unsigned long)info.nvram_crc32,
                                   (unsigned long)info.nvram_crc32_readback);
}

static void console_handle_frame(uint8_t *frame, uint16_t length)
{
    console_trim(frame, &length);
    if(length == 0U)
        return;

    if(strcmp((char *)frame, "help") == 0)
    {
        (void)app_uart4_console_write_string(
            "[console] commands: help, qspi id, qspi fwinfo, qspi crc <addr> <len>, ap power\r\n");
    }
    else if(strcmp((char *)frame, "qspi id") == 0)
    {
        console_cmd_qspi_id();
    }
    else if(strcmp((char *)frame, "qspi fwinfo") == 0)
    {
        console_cmd_qspi_fwinfo();
    }
    else if(console_starts_with((char *)frame, "qspi crc"))
    {
        console_cmd_qspi_crc((char *)frame);
    }
    else if(strcmp((char *)frame, "ap power") == 0)
    {
        bsp_ap6212_power_on();
        (void)app_uart4_console_write_string("[ap6212] power pins set\r\n");
    }
    else
    {
        (void)app_uart4_console_printf("[console] unknown command: %s\r\n", frame);
    }
}

static void console_thread_entry(ULONG thread_input)
{
    static const uint8_t ready[] = "[uart4-console] ldc_queue ready\r\n";

    (void)thread_input;

    if(ldc_queue_uart_start(&g_uart4_console_queue) != 0)
    {
        static const uint8_t failed[] = "[uart4-console] rx start failed\r\n";
        (void)app_uart4_console_write(failed, sizeof(failed) - 1U);
        return;
    }

    (void)app_uart4_console_write(ready, sizeof(ready) - 1U);

    for(;;)
    {
        (void)tx_semaphore_get(&g_uart4_console_packet_sem, TX_WAIT_FOREVER);

        for(;;)
        {
            int length = ldc_queue_pop(&g_uart4_console_queue,
                                       g_uart4_console_frame,
                                       sizeof(g_uart4_console_frame) - 1U);
            if(length <= 0)
                break;

            console_handle_frame(g_uart4_console_frame, (uint16_t)length);
        }
    }
}

UINT app_uart4_console_init(void)
{
    const app_ldc_port_config_t *ldc_config;
    ldc_queue_config_t queue_config;
    ldc_queue_uart_config_t uart_config;
    UINT status;

    if(g_uart4_console_initialized != 0U)
        return TX_SUCCESS;

    ldc_config = app_ldc_config_get(APP_LDC_PORT_UART4_ECHO);
    if(ldc_config == NULL)
        return TX_PTR_ERROR;

    status = tx_semaphore_create(&g_uart4_console_packet_sem,
                                 "UART4 Console Packet",
                                 0U);
    if(status != TX_SUCCESS)
        return status;

    status = tx_mutex_create(&g_uart4_console_tx_mutex,
                             "UART4 Console TX",
                             TX_NO_INHERIT);
    if(status != TX_SUCCESS)
        return status;

    queue_config.name = "uart4-console";
    queue_config.ring_buffer = g_uart4_console_ldc_ring;
    queue_config.ring_size = sizeof(g_uart4_console_ldc_ring);
    queue_config.packet_pool = g_uart4_console_ldc_packets;
    queue_config.packet_count = APP_UART4_ECHO_LDC_PACKET_COUNT;
    queue_config.max_frame = ldc_config->max_frame;
    queue_config.timeout_ms = ldc_config->timeout_ms;
    queue_config.delimiter = ldc_config->delimiter;
    queue_config.mode = LDC_QUEUE_MODE_PROTECT;
    queue_config.event_cb = console_queue_event;
    queue_config.event_arg = NULL;

    if(!ldc_queue_init(&g_uart4_console_queue, &queue_config))
        return TX_SIZE_ERROR;

    uart_config.port = APP_UART4_ECHO_PORT;
#if APP_UART4_ECHO_RX_DMA
    uart_config.rx_mode = LDC_QUEUE_UART_RX_DMA_BLOCK;
    uart_config.rx_buffer = g_uart4_console_rx_dma;
    uart_config.rx_buffer_size = sizeof(g_uart4_console_rx_dma);
#else
    uart_config.rx_mode = LDC_QUEUE_UART_RX_BYTE_IT;
    uart_config.rx_buffer = NULL;
    uart_config.rx_buffer_size = 0U;
#endif
    if(ldc_queue_bind_uart(&g_uart4_console_queue, &uart_config) != 0)
        return TX_PTR_ERROR;

    g_uart4_console_initialized = 1U;

    status = tx_timer_create(&g_uart4_console_tick_timer,
                             "UART4 Console Tick",
                             console_tick_timer_entry,
                             0U,
                             1U,
                             1U,
                             TX_AUTO_ACTIVATE);
    if(status != TX_SUCCESS)
        return status;

    status = tx_thread_create(&g_uart4_console_thread,
                              "UART4 Console",
                              console_thread_entry,
                              0U,
                              g_uart4_console_thread_stack,
                              sizeof(g_uart4_console_thread_stack),
                              APP_UART4_ECHO_THREAD_PRIO,
                              APP_UART4_ECHO_THREAD_PRIO,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status != TX_SUCCESS)
        return status;

    return TX_SUCCESS;
}

ldc_queue_t *app_uart4_console_queue(void)
{
    return &g_uart4_console_queue;
}
