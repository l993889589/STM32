#ifndef LDC_QUEUE_H
#define LDC_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_uart.h"
#include "ldc_core.h"

typedef enum
{
    LDC_QUEUE_MODE_PROTECT = 0,
    LDC_QUEUE_MODE_OVERWRITE
} ldc_queue_mode_t;

typedef enum
{
    LDC_QUEUE_UART_RX_DMA_BLOCK = 0,
    LDC_QUEUE_UART_RX_BYTE_IT
} ldc_queue_uart_rx_mode_t;

typedef enum
{
    LDC_QUEUE_EVT_PACKET = 0,
    LDC_QUEUE_EVT_OVERFLOW,
    LDC_QUEUE_EVT_DROP
} ldc_queue_event_t;

typedef struct ldc_queue ldc_queue_t;
typedef void (*ldc_queue_event_cb_t)(ldc_queue_t *queue,
                                     ldc_queue_event_t event,
                                     void *arg);

typedef struct
{
    const char *name;
    uint8_t *ring_buffer;
    uint32_t ring_size;
    ldc_packet_t *packet_pool;
    uint16_t packet_count;
    uint32_t max_frame;
    uint32_t timeout_ms;
    int delimiter;
    ldc_queue_mode_t mode;
    ldc_queue_event_cb_t event_cb;
    void *event_arg;
} ldc_queue_config_t;

typedef struct
{
    bsp_uart_port_t port;
    ldc_queue_uart_rx_mode_t rx_mode;
    uint8_t *rx_buffer;
    uint16_t rx_buffer_size;
} ldc_queue_uart_config_t;

struct ldc_queue
{
    ldc_t ldc;
    const char *name;
    uint8_t initialized;
    volatile uint8_t active;
    uint8_t uart_bound;
    bsp_uart_port_t uart_port;
    ldc_queue_uart_rx_mode_t uart_rx_mode;
    uint8_t *uart_rx_buffer;
    uint16_t uart_rx_buffer_size;
    ldc_queue_event_cb_t event_cb;
    void *event_arg;
    volatile uint32_t uart_rx_events;
    volatile uint32_t uart_rx_bytes;
    volatile uint32_t uart_rx_drops;
    volatile uint32_t packet_events;
    volatile uint32_t overflow_events;
    volatile uint32_t drop_events;
};

/* Initialize one caller-owned LDC queue. All buffers must remain valid after init. */
bool ldc_queue_init(ldc_queue_t *queue, const ldc_queue_config_t *config);

/* Add a received block, typically from DMA ReceiveToIdle or another packetized transport. */
uint32_t ldc_queue_add(ldc_queue_t *queue, const uint8_t *data, uint32_t length);

/* Add one received byte, typically from RXNE/HAL_UART_Receive_IT byte callbacks. */
bool ldc_queue_putc(ldc_queue_t *queue, uint8_t byte);

/* Account silence time and commit a pending frame when timeout_ms expires. */
void ldc_queue_tick(ldc_queue_t *queue, uint32_t elapsed_ms);

/* Manually commit the current incomplete frame. */
bool ldc_queue_flush(ldc_queue_t *queue);

/* Pop one complete frame. Returns frame length, 0 for none, -1 for invalid args or small buffer. */
int ldc_queue_pop(ldc_queue_t *queue, uint8_t *buffer, uint32_t size);

/* Return the number of complete frames waiting in the queue. */
uint16_t ldc_queue_available(ldc_queue_t *queue);

/* Return true while bytes have arrived but have not yet been committed as a frame. */
bool ldc_queue_frame_pending(ldc_queue_t *queue);

/* Copy LDC core counters for diagnostics. */
bool ldc_queue_get_stats(ldc_queue_t *queue, ldc_stats_t *stats);

/* Replace the queue event callback after init. The callback runs in the caller context that generated the event. */
void ldc_queue_set_event_callback(ldc_queue_t *queue,
                                  ldc_queue_event_cb_t callback,
                                  void *arg);

/* Return true when a timeout tick is useful because an incomplete frame is open. */
bool ldc_queue_need_tick(ldc_queue_t *queue);

/* Bind a queue to bsp_uart. Call ldc_queue_uart_start() after the bind succeeds. */
int ldc_queue_bind_uart(ldc_queue_t *queue, const ldc_queue_uart_config_t *config);

/* Start UART RX using either DMA/block receive or interrupt byte receive. */
int ldc_queue_uart_start(ldc_queue_t *queue);

/* Write through the bound UART using HAL_UART_Transmit(). */
int ldc_queue_uart_write(ldc_queue_t *queue, const uint8_t *data, uint16_t length, uint32_t timeout_ms);

/* Write through the bound UART and wait until the TC flag is set. */
int ldc_queue_uart_write_wait_complete(ldc_queue_t *queue,
                                       const uint8_t *data,
                                       uint16_t length,
                                       uint32_t timeout_ms);

#endif /* LDC_QUEUE_H */
