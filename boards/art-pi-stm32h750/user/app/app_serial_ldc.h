#ifndef APP_SERIAL_LDC_H
#define APP_SERIAL_LDC_H

#include <stdbool.h>
#include <stdint.h>

#include "app_ldc_config.h"
#include "bsp_uart.h"
#include "ldc_endpoint_threadx.h"
#include "tx_api.h"

#define APP_SERIAL_LDC_FLAG_ECHO_RX        (1UL << 0)

typedef struct app_serial_ldc app_serial_ldc_t;

typedef void (*app_serial_ldc_frame_cb_t)(app_serial_ldc_t *serial,
                                          const uint8_t *frame,
                                          uint16_t length,
                                          void *arg);

typedef struct
{
    const char *name;
    bsp_uart_port_t uart_port;
    const app_ldc_port_config_t *ldc_config;

    uint8_t *rx_dma;
    uint16_t rx_dma_size;

    ULONG *tx_queue_storage;
    uint32_t tx_queue_depth;
    uint8_t *tx_chunk;
    uint16_t tx_chunk_size;

    UCHAR *thread_stack;
    uint32_t thread_stack_size;
    UINT thread_priority;

    uint8_t *ldc_ring;
    uint32_t ldc_ring_size;
    ldc_packet_t *ldc_packets;
    uint16_t ldc_packet_count;
    uint8_t *frame_buffer;
    uint16_t frame_buffer_size;

    uint32_t flags;
    app_serial_ldc_frame_cb_t frame_cb;
    void *frame_arg;
} app_serial_ldc_config_t;

typedef struct
{
    volatile uint32_t init_status;
    volatile uint32_t thread_started;
    volatile int32_t rx_start_status;
    volatile uint32_t rx_events;
    volatile uint32_t rx_bytes;
    volatile uint32_t ldc_bytes;
    volatile uint32_t ldc_drops;
    volatile uint32_t tx_queued_bytes;
    volatile uint32_t tx_queue_drops;
    volatile uint32_t tx_start_ok;
    volatile uint32_t tx_start_busy;
    volatile uint32_t tx_start_error;
    volatile uint32_t tx_done_events;
    volatile uint32_t tx_bytes;
    volatile uint32_t frames;
    volatile uint32_t last_rx_len;
    volatile uint32_t last_tx_len;
    volatile uint32_t last_frame_len;
} app_serial_ldc_diag_t;

struct app_serial_ldc
{
    TX_QUEUE tx_queue;
    TX_SEMAPHORE tx_done;
    TX_THREAD thread;
    ldc_endpoint_t endpoint;

    const char *name;
    bsp_uart_port_t uart_port;
    uint8_t *rx_dma;
    uint16_t rx_dma_size;
    ULONG *tx_queue_storage;
    uint32_t tx_queue_depth;
    uint8_t *tx_chunk;
    uint16_t tx_chunk_size;
    UCHAR *thread_stack;
    uint32_t thread_stack_size;
    UINT thread_priority;
    uint8_t *frame_buffer;
    uint16_t frame_buffer_size;
    uint32_t flags;
    app_serial_ldc_frame_cb_t frame_cb;
    void *frame_arg;
    uint8_t initialized;
    uint8_t tx_queue_enabled;
    app_serial_ldc_diag_t diag;
};

UINT app_serial_ldc_init(app_serial_ldc_t *serial,
                         const app_serial_ldc_config_t *config);
int app_serial_ldc_send_async(app_serial_ldc_t *serial,
                              const uint8_t *data,
                              uint16_t length);
int app_serial_ldc_send_blocking(app_serial_ldc_t *serial,
                                 const uint8_t *data,
                                 uint16_t length,
                                 uint32_t timeout_ms);
int app_serial_ldc_read_frame(app_serial_ldc_t *serial,
                              uint8_t *buffer,
                              uint16_t size);
ldc_endpoint_t *app_serial_ldc_endpoint(app_serial_ldc_t *serial);
const app_serial_ldc_diag_t *app_serial_ldc_diag(app_serial_ldc_t *serial);
bool app_serial_ldc_get_ldc_stats(app_serial_ldc_t *serial, ldc_stats_t *stats);

#endif /* APP_SERIAL_LDC_H */
