#include "ldc_queue.h"

#include <string.h>

#include "main.h"

static ldc_mode_t ldc_queue_map_mode(ldc_queue_mode_t mode)
{
    return (mode == LDC_QUEUE_MODE_OVERWRITE) ? LDC_MODE_OVERWRITE : LDC_MODE_PROTECT;
}

static ldc_queue_event_t ldc_queue_map_event(ldc_event_t event)
{
    if(event == LDC_EVT_PACKET)
        return LDC_QUEUE_EVT_PACKET;
    if(event == LDC_EVT_OVERFLOW)
        return LDC_QUEUE_EVT_OVERFLOW;
    return LDC_QUEUE_EVT_DROP;
}

static uint32_t ldc_queue_lock(void *arg)
{
    uint32_t state;

    (void)arg;
    state = __get_PRIMASK();
    __disable_irq();
    return state;
}

static void ldc_queue_unlock(void *arg, uint32_t state)
{
    (void)arg;
    __set_PRIMASK(state);
}

static bool ldc_queue_config_valid(const ldc_queue_config_t *config)
{
    return config != NULL &&
           config->name != NULL &&
           config->ring_buffer != NULL &&
           config->ring_size >= 2U &&
           config->packet_pool != NULL &&
           config->packet_count != 0U;
}

static void ldc_queue_ldc_event(void *arg, ldc_event_t event)
{
    ldc_queue_t *queue = (ldc_queue_t *)arg;
    ldc_queue_event_t queue_event;

    if(queue == NULL)
        return;

    queue_event = ldc_queue_map_event(event);
    if(queue_event == LDC_QUEUE_EVT_PACKET)
        queue->packet_events++;
    else if(queue_event == LDC_QUEUE_EVT_OVERFLOW)
        queue->overflow_events++;
    else
        queue->drop_events++;

    if(queue->event_cb != NULL)
        queue->event_cb(queue, queue_event, queue->event_arg);
}

bool ldc_queue_init(ldc_queue_t *queue, const ldc_queue_config_t *config)
{
    if(queue == NULL || !ldc_queue_config_valid(config))
        return false;

    memset(queue, 0, sizeof(*queue));
    if(!ldc_init(&queue->ldc,
                 config->ring_buffer,
                 config->ring_size,
                 config->packet_pool,
                 config->packet_count))
        return false;

    queue->name = config->name;
    queue->event_cb = config->event_cb;
    queue->event_arg = config->event_arg;
    ldc_set_lock(&queue->ldc, ldc_queue_lock, ldc_queue_unlock, queue);
    ldc_set_mode(&queue->ldc, ldc_queue_map_mode(config->mode));
    ldc_set_frame_config(&queue->ldc,
                         config->max_frame,
                         config->timeout_ms,
                         config->delimiter);
    ldc_set_callback(&queue->ldc, ldc_queue_ldc_event, queue);
    queue->initialized = 1U;
    return true;
}

uint32_t ldc_queue_add(ldc_queue_t *queue, const uint8_t *data, uint32_t length)
{
    uint32_t written;

    if(queue == NULL || queue->initialized == 0U)
        return 0U;

    written = ldc_write(&queue->ldc, data, length);
    if(written != 0U)
        queue->active = ldc_frame_pending(&queue->ldc) ? 1U : 0U;

    return written;
}

bool ldc_queue_putc(ldc_queue_t *queue, uint8_t byte)
{
    bool written;

    if(queue == NULL || queue->initialized == 0U)
        return false;

    written = ldc_putc(&queue->ldc, byte);
    if(written)
        queue->active = ldc_frame_pending(&queue->ldc) ? 1U : 0U;

    return written;
}

void ldc_queue_tick(ldc_queue_t *queue, uint32_t elapsed_ms)
{
    if(queue == NULL || queue->initialized == 0U)
        return;

    ldc_tick(&queue->ldc, elapsed_ms);
    queue->active = ldc_frame_pending(&queue->ldc) ? 1U : 0U;
}

bool ldc_queue_flush(ldc_queue_t *queue)
{
    bool flushed;

    if(queue == NULL || queue->initialized == 0U)
        return false;

    flushed = ldc_flush(&queue->ldc);
    queue->active = ldc_frame_pending(&queue->ldc) ? 1U : 0U;
    return flushed;
}

int ldc_queue_pop(ldc_queue_t *queue, uint8_t *buffer, uint32_t size)
{
    if(queue == NULL || queue->initialized == 0U)
        return -1;

    return ldc_read_packet(&queue->ldc, buffer, size);
}

uint16_t ldc_queue_available(ldc_queue_t *queue)
{
    if(queue == NULL || queue->initialized == 0U)
        return 0U;

    return ldc_packet_available(&queue->ldc);
}

bool ldc_queue_frame_pending(ldc_queue_t *queue)
{
    if(queue == NULL || queue->initialized == 0U)
        return false;

    return ldc_frame_pending(&queue->ldc);
}

bool ldc_queue_get_stats(ldc_queue_t *queue, ldc_stats_t *stats)
{
    if(queue == NULL || queue->initialized == 0U || stats == NULL)
        return false;

    return ldc_get_stats(&queue->ldc, stats);
}

void ldc_queue_set_event_callback(ldc_queue_t *queue,
                                  ldc_queue_event_cb_t callback,
                                  void *arg)
{
    if(queue == NULL || queue->initialized == 0U)
        return;

    queue->event_cb = callback;
    queue->event_arg = arg;
}

bool ldc_queue_need_tick(ldc_queue_t *queue)
{
    return queue != NULL &&
           queue->initialized != 0U &&
           queue->active != 0U;
}

static void ldc_queue_uart_rx(bsp_uart_port_t port,
                              const uint8_t *data,
                              uint16_t length,
                              void *arg)
{
    ldc_queue_t *queue = (ldc_queue_t *)arg;
    uint32_t written = 0U;

    (void)port;
    if(queue == NULL || data == NULL || length == 0U)
        return;

    queue->uart_rx_events++;
    queue->uart_rx_bytes += length;

    if(queue->uart_rx_mode == LDC_QUEUE_UART_RX_BYTE_IT)
    {
        for(uint16_t i = 0U; i < length; i++)
        {
            if(ldc_queue_putc(queue, data[i]))
                written++;
        }
    }
    else
    {
        written = ldc_queue_add(queue, data, length);
    }

    if(written < length)
        queue->uart_rx_drops += (uint32_t)length - written;
}

int ldc_queue_bind_uart(ldc_queue_t *queue, const ldc_queue_uart_config_t *config)
{
    if(queue == NULL || queue->initialized == 0U || config == NULL)
        return -1;

    if(config->rx_mode == LDC_QUEUE_UART_RX_DMA_BLOCK &&
       (config->rx_buffer == NULL || config->rx_buffer_size == 0U))
        return -1;

    queue->uart_port = config->port;
    queue->uart_rx_mode = config->rx_mode;
    queue->uart_rx_buffer = config->rx_buffer;
    queue->uart_rx_buffer_size = config->rx_buffer_size;

    if(bsp_uart_register_rx_callback(config->port, ldc_queue_uart_rx, queue) != 0)
        return -1;

    queue->uart_bound = 1U;
    return 0;
}

int ldc_queue_uart_start(ldc_queue_t *queue)
{
    if(queue == NULL || queue->initialized == 0U || queue->uart_bound == 0U)
        return -1;

    if(queue->uart_rx_mode == LDC_QUEUE_UART_RX_BYTE_IT)
        return bsp_uart_start_rx_byte(queue->uart_port);

    return bsp_uart_start_rx(queue->uart_port,
                             queue->uart_rx_buffer,
                             queue->uart_rx_buffer_size);
}

int ldc_queue_uart_write(ldc_queue_t *queue,
                         const uint8_t *data,
                         uint16_t length,
                         uint32_t timeout_ms)
{
    if(queue == NULL || queue->initialized == 0U || queue->uart_bound == 0U)
        return -1;

    return bsp_uart_write(queue->uart_port, data, length, timeout_ms);
}

int ldc_queue_uart_write_wait_complete(ldc_queue_t *queue,
                                       const uint8_t *data,
                                       uint16_t length,
                                       uint32_t timeout_ms)
{
    if(queue == NULL || queue->initialized == 0U || queue->uart_bound == 0U)
        return -1;

    return bsp_uart_write_wait_complete(queue->uart_port, data, length, timeout_ms);
}
