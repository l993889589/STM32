#include "app_ap6212_bridge.h"

#include <stdint.h>

#include "app_config.h"
#include "app_ldc_config.h"
#include "app_qspi_loader.h"
#include "bsp.h"

static app_serial_ldc_t g_debug_serial;
static app_serial_ldc_t g_ap6212_bt_serial;

static ULONG g_debug_tx_queue_storage[APP_UART4_ECHO_TX_QUEUE_DEPTH];
static UCHAR g_debug_thread_stack[APP_UART4_ECHO_STACK_SIZE];
static uint8_t g_debug_rx_dma[APP_UART4_ECHO_RX_DMA_SIZE] __ALIGNED(32);
static uint8_t g_debug_tx_chunk[APP_UART4_ECHO_TX_CHUNK_SIZE];
static uint8_t g_debug_ldc_ring[APP_UART4_ECHO_LDC_RING_SIZE + 1U];
static ldc_packet_t g_debug_ldc_packets[APP_UART4_ECHO_LDC_PACKET_COUNT];
static uint8_t g_debug_frame[APP_UART4_ECHO_LDC_MAX_FRAME];

static ULONG g_ap6212_tx_queue_storage[APP_AP6212_BT_TX_QUEUE_DEPTH];
static UCHAR g_ap6212_thread_stack[APP_AP6212_BT_STACK_SIZE];
static uint8_t g_ap6212_rx_dma[APP_AP6212_BT_RX_DMA_SIZE] __ALIGNED(32);
static uint8_t g_ap6212_tx_chunk[APP_AP6212_BT_TX_CHUNK_SIZE];
static uint8_t g_ap6212_ldc_ring[APP_AP6212_BT_LDC_RING_SIZE + 1U];
static ldc_packet_t g_ap6212_ldc_packets[APP_AP6212_BT_LDC_PACKET_COUNT];
static uint8_t g_ap6212_frame[APP_AP6212_BT_LDC_MAX_FRAME];

static void bridge_frame_to_target(app_serial_ldc_t *serial,
                                   const uint8_t *frame,
                                   uint16_t length,
                                   void *arg)
{
    app_serial_ldc_t *target = (app_serial_ldc_t *)arg;

    (void)serial;
    if(serial == &g_debug_serial &&
       app_qspi_loader_handle_frame(serial, frame, length))
        return;

    (void)app_serial_ldc_send_async(target, frame, length);
}

static UINT init_debug_serial(void)
{
    const app_ldc_port_config_t *ldc_config;
    app_serial_ldc_config_t config;

    ldc_config = app_ldc_config_get(APP_LDC_PORT_UART4_ECHO);
    if(ldc_config == NULL)
        return TX_PTR_ERROR;

    config.name = "uart4-debug";
    config.uart_port = APP_UART4_ECHO_PORT;
    config.ldc_config = ldc_config;
    config.rx_dma = g_debug_rx_dma;
    config.rx_dma_size = sizeof(g_debug_rx_dma);
    config.tx_queue_storage = g_debug_tx_queue_storage;
    config.tx_queue_depth = APP_UART4_ECHO_TX_QUEUE_DEPTH;
    config.tx_chunk = g_debug_tx_chunk;
    config.tx_chunk_size = sizeof(g_debug_tx_chunk);
    config.thread_stack = g_debug_thread_stack;
    config.thread_stack_size = sizeof(g_debug_thread_stack);
    config.thread_priority = APP_UART4_ECHO_THREAD_PRIO;
    config.ldc_ring = g_debug_ldc_ring;
    config.ldc_ring_size = sizeof(g_debug_ldc_ring);
    config.ldc_packets = g_debug_ldc_packets;
    config.ldc_packet_count = APP_UART4_ECHO_LDC_PACKET_COUNT;
    config.frame_buffer = g_debug_frame;
    config.frame_buffer_size = sizeof(g_debug_frame);
    config.flags = 0U;
    config.frame_cb = bridge_frame_to_target;
    config.frame_arg = &g_ap6212_bt_serial;

    return app_serial_ldc_init(&g_debug_serial, &config);
}

static UINT init_ap6212_bt_serial(void)
{
    const app_ldc_port_config_t *ldc_config;
    app_serial_ldc_config_t config;

    ldc_config = app_ldc_config_get(APP_LDC_PORT_AP6212_BT);
    if(ldc_config == NULL)
        return TX_PTR_ERROR;

    config.name = "ap6212-bt";
    config.uart_port = APP_AP6212_BT_UART_PORT;
    config.ldc_config = ldc_config;
    config.rx_dma = g_ap6212_rx_dma;
    config.rx_dma_size = sizeof(g_ap6212_rx_dma);
    config.tx_queue_storage = g_ap6212_tx_queue_storage;
    config.tx_queue_depth = APP_AP6212_BT_TX_QUEUE_DEPTH;
    config.tx_chunk = g_ap6212_tx_chunk;
    config.tx_chunk_size = sizeof(g_ap6212_tx_chunk);
    config.thread_stack = g_ap6212_thread_stack;
    config.thread_stack_size = sizeof(g_ap6212_thread_stack);
    config.thread_priority = APP_AP6212_BT_THREAD_PRIO;
    config.ldc_ring = g_ap6212_ldc_ring;
    config.ldc_ring_size = sizeof(g_ap6212_ldc_ring);
    config.ldc_packets = g_ap6212_ldc_packets;
    config.ldc_packet_count = APP_AP6212_BT_LDC_PACKET_COUNT;
    config.frame_buffer = g_ap6212_frame;
    config.frame_buffer_size = sizeof(g_ap6212_frame);
    config.flags = 0U;
    config.frame_cb = bridge_frame_to_target;
    config.frame_arg = &g_debug_serial;

    return app_serial_ldc_init(&g_ap6212_bt_serial, &config);
}

UINT app_ap6212_bridge_init(void)
{
    static const uint8_t ready[] =
        "\r\n[ap6212-bt-ldc] ready, wifi firmware/netx skipped\r\n";
    UINT status;

    bsp_ap6212_power_on();

    status = init_ap6212_bt_serial();
    if(status != TX_SUCCESS)
        return status;

    status = init_debug_serial();
    if(status == TX_SUCCESS)
        (void)app_serial_ldc_send_async(&g_debug_serial,
                                        ready,
                                        sizeof(ready) - 1U);

    return status;
}

app_serial_ldc_t *app_ap6212_bridge_debug_serial(void)
{
    return &g_debug_serial;
}

app_serial_ldc_t *app_ap6212_bridge_bt_serial(void)
{
    return &g_ap6212_bt_serial;
}
