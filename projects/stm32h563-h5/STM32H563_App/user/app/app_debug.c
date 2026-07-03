#include "app_debug.h"

#include <string.h>

#include "bsp_uart.h"
#include "ldc_easy.h"
#include "ldc_port_irq.h"

#define APP_DEBUG_MAX_FRAME       256U
#define APP_DEBUG_PACKET_COUNT    4U
#define APP_DEBUG_IDLE_TIMEOUT_MS 30U
#define APP_DEBUG_TX_TIMEOUT_MS   1000U

static ldc_easy_t g_debug_ldc;
static uint8_t g_debug_ring[LDC_EASY_RING_BYTES(APP_DEBUG_MAX_FRAME,
                                                APP_DEBUG_PACKET_COUNT)];
static ldc_packet_t g_debug_packets[APP_DEBUG_PACKET_COUNT];
static uint8_t g_debug_frame[APP_DEBUG_MAX_FRAME];

static TX_THREAD g_debug_thread;
static TX_SEMAPHORE g_debug_rx_sem;
static UCHAR g_debug_thread_stack[1024];

static const bsp_uart_port_t g_debug_port = BSP_UART_DEBUG;
static uint8_t g_debug_initialized;

static void app_debug_ldc_event(ldc_easy_t *queue, ldc_easy_event_t event, void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_EASY_EVT_PACKET)
    {
        (void)tx_semaphore_put(&g_debug_rx_sem);
    }
}

static void app_debug_uart_rx(bsp_uart_port_t port,
                              const uint8_t *data,
                              uint16_t len,
                              void *arg)
{
    uint32_t written;

    (void)arg;

    if(port != g_debug_port || data == NULL || len == 0U)
        return;

    written = ldc_easy_add(&g_debug_ldc, data, len);
    if(written != (uint32_t)len)
    {
        (void)ldc_easy_abort(&g_debug_ldc);
    }
}

static void app_debug_echo_packet(const uint8_t *data, uint16_t len)
{
    if(data != NULL && len != 0U)
    {
        (void)bsp_uart_write_wait_complete(g_debug_port,
                                           data,
                                           len,
                                           APP_DEBUG_TX_TIMEOUT_MS);
    }
}

static void app_debug_drain_packets(void)
{
    for(;;)
    {
        int len = ldc_easy_pop(&g_debug_ldc,
                               g_debug_frame,
                               (uint32_t)sizeof(g_debug_frame));
        if(len <= 0)
            break;

        app_debug_echo_packet(g_debug_frame, (uint16_t)len);
    }
}

static void app_debug_thread_entry(ULONG thread_input)
{
    (void)thread_input;

    for(;;)
    {
        if(tx_semaphore_get(&g_debug_rx_sem, TX_WAIT_FOREVER) != TX_SUCCESS)
            continue;

        app_debug_drain_packets();

        while(tx_semaphore_get(&g_debug_rx_sem, TX_NO_WAIT) == TX_SUCCESS)
        {
        }

        app_debug_drain_packets();
    }
}

UINT app_debug_init(void)
{
    ldc_easy_config_t config;
    UINT status;

    if(g_debug_initialized != 0U)
        return TX_SUCCESS;

    status = tx_semaphore_create(&g_debug_rx_sem, "debug rx", 0U);
    if(status != TX_SUCCESS)
        return status;

    (void)memset(&config, 0, sizeof(config));
    config.ring_buffer = g_debug_ring;
    config.ring_size = (uint32_t)sizeof(g_debug_ring);
    config.packet_pool = g_debug_packets;
    config.packet_count = APP_DEBUG_PACKET_COUNT;
    config.max_frame = APP_DEBUG_MAX_FRAME;
    config.timeout_ms = APP_DEBUG_IDLE_TIMEOUT_MS;
    config.delimiter_enabled = true;
    config.delimiter = (uint8_t)'\n';
    config.mode = LDC_MODE_PROTECT;
    config.lock = ldc_port_irq_lock;
    config.unlock = ldc_port_irq_unlock;
    config.event_cb = app_debug_ldc_event;
    config.auto_tick = true;

    if(!ldc_easy_init(&g_debug_ldc, &config))
        return TX_SIZE_ERROR;

    if(bsp_uart_register_rx_callback(g_debug_port, app_debug_uart_rx, NULL) != 0 ||
       bsp_uart_start_rx_byte(g_debug_port) != 0)
    {
        return TX_START_ERROR;
    }

    status = tx_thread_create(&g_debug_thread,
                              "App Debug",
                              app_debug_thread_entry,
                              0U,
                              g_debug_thread_stack,
                              sizeof(g_debug_thread_stack),
                              14U,
                              14U,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status != TX_SUCCESS)
        return status;

    g_debug_initialized = 1U;
    return TX_SUCCESS;
}

bool app_debug_get_stats(ldc_stats_t *stats)
{
    return ldc_easy_get_stats(&g_debug_ldc, stats);
}
