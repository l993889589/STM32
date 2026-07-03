#include "app_uart4_echo.h"

#include <stdint.h>

#include "app_config.h"
#include "app_ldc_config.h"

static ldc_queue_t g_uart4_queue;
static TX_THREAD g_uart4_thread;
static TX_SEMAPHORE g_uart4_packet_sem;
static TX_TIMER g_uart4_tick_timer;

static UCHAR g_uart4_thread_stack[APP_UART4_ECHO_STACK_SIZE];

static uint8_t g_uart4_rx_dma[APP_UART4_ECHO_RX_DMA_SIZE] __ALIGNED(32);
static uint8_t g_uart4_ldc_ring[APP_UART4_ECHO_LDC_RING_SIZE + 1U];
static ldc_packet_t g_uart4_ldc_packets[APP_UART4_ECHO_LDC_PACKET_COUNT];
static uint8_t g_uart4_frame[APP_UART4_ECHO_LDC_MAX_FRAME];
static volatile uint32_t g_uart4_packet_signals;
static volatile uint32_t g_uart4_drop_events;
static volatile uint32_t g_uart4_overflow_events;

static void app_uart4_write(const uint8_t *data, uint16_t length)
{
    (void)ldc_queue_uart_write_wait_complete(&g_uart4_queue,
                                             data,
                                             length,
                                             APP_SHELL_TX_TIMEOUT_MS);
}

static void app_uart4_queue_event(ldc_queue_t *queue,
                                  ldc_queue_event_t event,
                                  void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_QUEUE_EVT_PACKET)
    {
        g_uart4_packet_signals++;
        (void)tx_semaphore_put(&g_uart4_packet_sem);
    }
    else if(event == LDC_QUEUE_EVT_DROP)
    {
        g_uart4_drop_events++;
    }
    else
    {
        g_uart4_overflow_events++;
    }
}

static void app_uart4_tick_timer_entry(ULONG timer_input)
{
    static uint32_t last_ms;
    uint32_t now_ms;
    uint32_t elapsed_ms;

    (void)timer_input;

    now_ms = HAL_GetTick();
    elapsed_ms = now_ms - last_ms;
    last_ms = now_ms;

    if(elapsed_ms == 0U || !ldc_queue_need_tick(&g_uart4_queue))
        return;

    ldc_queue_tick(&g_uart4_queue, elapsed_ms);
}

static void app_uart4_echo_task_entry(ULONG thread_input)
{
    static const uint8_t ready[] = "[uart4-ldc-queue] ready\r\n";
    static const uint8_t prefix[] = "[uart4-ldc-queue] frame: ";
    static const uint8_t newline[] = "\r\n";

    (void)thread_input;

    if(ldc_queue_uart_start(&g_uart4_queue) != 0)
    {
        static const uint8_t failed[] = "[uart4-ldc-queue] rx start failed\r\n";
        app_uart4_write(failed, sizeof(failed) - 1U);
        return;
    }

    app_uart4_write(ready, sizeof(ready) - 1U);

    for(;;)
    {
        (void)tx_semaphore_get(&g_uart4_packet_sem, TX_WAIT_FOREVER);

        for(;;)
        {
            int length = ldc_queue_pop(&g_uart4_queue,
                                       g_uart4_frame,
                                       sizeof(g_uart4_frame));
            if(length <= 0)
                break;

            app_uart4_write(prefix, sizeof(prefix) - 1U);
            app_uart4_write(g_uart4_frame, (uint16_t)length);
            app_uart4_write(newline, sizeof(newline) - 1U);
        }

        tx_thread_sleep(1U);
    }
}

UINT app_uart4_echo_init(void)
{
    const app_ldc_port_config_t *ldc_config;
    ldc_queue_config_t queue_config;
    ldc_queue_uart_config_t uart_config;
    UINT status;

    ldc_config = app_ldc_config_get(APP_LDC_PORT_UART4_ECHO);
    if(ldc_config == NULL)
        return TX_PTR_ERROR;

    status = tx_semaphore_create(&g_uart4_packet_sem,
                                 "UART4 LDC Packet",
                                 0U);
    if(status != TX_SUCCESS)
        return status;

    queue_config.name = "uart4-ldc-queue";
    queue_config.ring_buffer = g_uart4_ldc_ring;
    queue_config.ring_size = sizeof(g_uart4_ldc_ring);
    queue_config.packet_pool = g_uart4_ldc_packets;
    queue_config.packet_count = APP_UART4_ECHO_LDC_PACKET_COUNT;
    queue_config.max_frame = ldc_config->max_frame;
    queue_config.timeout_ms = ldc_config->timeout_ms;
    queue_config.delimiter = ldc_config->delimiter;
    queue_config.mode = LDC_QUEUE_MODE_PROTECT;
    queue_config.event_cb = app_uart4_queue_event;
    queue_config.event_arg = NULL;

    if(!ldc_queue_init(&g_uart4_queue, &queue_config))
        return TX_SIZE_ERROR;

    uart_config.port = APP_UART4_ECHO_PORT;
#if APP_UART4_ECHO_RX_DMA
    uart_config.rx_mode = LDC_QUEUE_UART_RX_DMA_BLOCK;
    uart_config.rx_buffer = g_uart4_rx_dma;
    uart_config.rx_buffer_size = sizeof(g_uart4_rx_dma);
#else
    uart_config.rx_mode = LDC_QUEUE_UART_RX_BYTE_IT;
    uart_config.rx_buffer = NULL;
    uart_config.rx_buffer_size = 0U;
#endif
    if(ldc_queue_bind_uart(&g_uart4_queue, &uart_config) != 0)
        return TX_PTR_ERROR;

    status = tx_timer_create(&g_uart4_tick_timer,
                             "UART4 LDC Tick",
                             app_uart4_tick_timer_entry,
                             0U,
                             1U,
                             1U,
                             TX_AUTO_ACTIVATE);
    if(status != TX_SUCCESS)
        return status;

    status = tx_thread_create(&g_uart4_thread,
                              "UART4 LDC Queue",
                              app_uart4_echo_task_entry,
                              0U,
                              g_uart4_thread_stack,
                              sizeof(g_uart4_thread_stack),
                              APP_UART4_ECHO_THREAD_PRIO,
                              APP_UART4_ECHO_THREAD_PRIO,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status != TX_SUCCESS)
        return status;

    return TX_SUCCESS;
}

ldc_queue_t *app_uart4_echo_queue(void)
{
    return &g_uart4_queue;
}
