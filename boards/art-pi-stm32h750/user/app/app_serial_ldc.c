#include "app_serial_ldc.h"

#include <string.h>

static int app_serial_ldc_queue_bytes(app_serial_ldc_t *serial,
                                      const uint8_t *data,
                                      uint16_t length)
{
    uint16_t queued = 0U;

    if(serial == NULL || data == NULL || length == 0U ||
       serial->tx_queue_enabled == 0U)
        return -1;

    for(uint16_t i = 0U; i < length; i++)
    {
        ULONG msg = data[i];
        if(tx_queue_send(&serial->tx_queue, &msg, TX_NO_WAIT) != TX_SUCCESS)
        {
            serial->diag.tx_queue_drops += (uint32_t)(length - i);
            break;
        }
        queued++;
    }

    serial->diag.tx_queued_bytes += queued;
    return (int)queued;
}

static void app_serial_ldc_tx_done(bsp_uart_port_t port, void *arg)
{
    app_serial_ldc_t *serial = (app_serial_ldc_t *)arg;

    (void)port;
    if(serial == NULL)
        return;

    serial->diag.tx_done_events++;
    (void)tx_semaphore_put(&serial->tx_done);
}

static void app_serial_ldc_rx(bsp_uart_port_t port,
                              const uint8_t *data,
                              uint16_t length,
                              void *arg)
{
    app_serial_ldc_t *serial = (app_serial_ldc_t *)arg;
    uint32_t written;

    (void)port;
    if(serial == NULL || data == NULL || length == 0U)
        return;

    serial->diag.rx_events++;
    serial->diag.rx_bytes += length;
    serial->diag.last_rx_len = length;

    written = ldc_endpoint_write(&serial->endpoint, data, length);
    serial->diag.ldc_bytes += written;
    if(written != length)
        serial->diag.ldc_drops += (uint32_t)length - written;

    if((serial->flags & APP_SERIAL_LDC_FLAG_ECHO_RX) != 0U)
        (void)app_serial_ldc_queue_bytes(serial, data, length);
}

static void app_serial_ldc_send_chunk(app_serial_ldc_t *serial, uint8_t first)
{
    uint16_t length = 1U;
    int result;

    if(serial == NULL || serial->tx_chunk == NULL || serial->tx_chunk_size == 0U)
        return;

    serial->tx_chunk[0] = first;
    while(length < serial->tx_chunk_size)
    {
        ULONG msg;
        if(tx_queue_receive(&serial->tx_queue, &msg, TX_NO_WAIT) != TX_SUCCESS)
            break;
        serial->tx_chunk[length++] = (uint8_t)msg;
    }

    while(tx_semaphore_get(&serial->tx_done, TX_NO_WAIT) == TX_SUCCESS)
    {
    }

    do
    {
        result = bsp_uart_write_it(serial->uart_port, serial->tx_chunk, length);
        if(result == 0)
        {
            serial->diag.tx_start_busy++;
            tx_thread_sleep(1U);
        }
    } while(result == 0);

    if(result < 0)
    {
        serial->diag.tx_start_error++;
        return;
    }

    serial->diag.tx_start_ok++;
    serial->diag.last_tx_len = length;
    serial->diag.tx_bytes += length;
    (void)tx_semaphore_get(&serial->tx_done, TX_WAIT_FOREVER);
}

static void app_serial_ldc_process_frames(app_serial_ldc_t *serial)
{
    int length;

    if(serial == NULL || serial->frame_cb == NULL ||
       serial->frame_buffer == NULL || serial->frame_buffer_size == 0U)
        return;

    while((length = ldc_endpoint_read(&serial->endpoint,
                                      serial->frame_buffer,
                                      serial->frame_buffer_size)) > 0)
    {
        serial->diag.frames++;
        serial->diag.last_frame_len = (uint32_t)length;
        serial->frame_cb(serial,
                         serial->frame_buffer,
                         (uint16_t)length,
                         serial->frame_arg);
    }
}

static void app_serial_ldc_thread_entry(ULONG thread_input)
{
    app_serial_ldc_t *serial = (app_serial_ldc_t *)thread_input;

    if(serial == NULL)
        return;

    serial->diag.thread_started = 1U;
    serial->diag.rx_start_status =
        bsp_uart_start_rx(serial->uart_port, serial->rx_dma, serial->rx_dma_size);

    for(;;)
    {
        ULONG msg;
        ULONG events;

        if(serial->tx_queue_enabled != 0U &&tx_queue_receive(&serial->tx_queue, &msg, 1U) == TX_SUCCESS)
        {
            app_serial_ldc_send_chunk(serial, (uint8_t)msg);
        }

        (void)ldc_endpoint_wait_for(&serial->endpoint, 1U, &events);
        app_serial_ldc_process_frames(serial);
    }
}

static bool app_serial_ldc_config_valid(const app_serial_ldc_config_t *config)
{
    if(config == NULL || config->name == NULL || config->ldc_config == NULL ||
       config->rx_dma == NULL || config->rx_dma_size == 0U ||
       config->thread_stack == NULL || config->thread_stack_size == 0U ||
       config->ldc_ring == NULL || config->ldc_ring_size == 0U ||
       config->ldc_packets == NULL || config->ldc_packet_count == 0U)
        return false;

    if((config->flags & APP_SERIAL_LDC_FLAG_ECHO_RX) != 0U &&
       (config->tx_queue_storage == NULL || config->tx_queue_depth == 0U ||
        config->tx_chunk == NULL || config->tx_chunk_size == 0U))
        return false;

    if(config->frame_cb != NULL &&
       (config->frame_buffer == NULL || config->frame_buffer_size == 0U))
        return false;

    return true;
}

UINT app_serial_ldc_init(app_serial_ldc_t *serial,
                         const app_serial_ldc_config_t *config)
{
    ldc_endpoint_config_t endpoint_config;
    UINT status;

    if(serial == NULL || !app_serial_ldc_config_valid(config))
        return TX_PTR_ERROR;

    if(serial->initialized != 0U)
        return TX_SUCCESS;

    memset(serial, 0, sizeof(*serial));

    serial->name = config->name;
    serial->uart_port = config->uart_port;
    serial->rx_dma = config->rx_dma;
    serial->rx_dma_size = config->rx_dma_size;
    serial->tx_queue_storage = config->tx_queue_storage;
    serial->tx_queue_depth = config->tx_queue_depth;
    serial->tx_chunk = config->tx_chunk;
    serial->tx_chunk_size = config->tx_chunk_size;
    serial->thread_stack = config->thread_stack;
    serial->thread_stack_size = config->thread_stack_size;
    serial->thread_priority = config->thread_priority;
    serial->frame_buffer = config->frame_buffer;
    serial->frame_buffer_size = config->frame_buffer_size;
    serial->flags = config->flags;
    serial->frame_cb = config->frame_cb;
    serial->frame_arg = config->frame_arg;
    serial->diag.init_status = 0xFFFFFFFFU;
    serial->diag.rx_start_status = -1;

    endpoint_config.name = config->ldc_config->name;
    endpoint_config.ring_buffer = config->ldc_ring;
    endpoint_config.ring_size = config->ldc_ring_size;
    endpoint_config.packet_pool = config->ldc_packets;
    endpoint_config.packet_count = config->ldc_packet_count;
    endpoint_config.max_frame = config->ldc_config->max_frame;
    endpoint_config.timeout_ms = config->ldc_config->timeout_ms;
    endpoint_config.delimiter = config->ldc_config->delimiter;
    endpoint_config.mode = LDC_MODE_OVERWRITE;
    status = ldc_endpoint_init(&serial->endpoint, &endpoint_config);
    if(status != TX_SUCCESS)
    {
        serial->diag.init_status = status;
        return status;
    }

    if(config->tx_queue_storage != NULL && config->tx_queue_depth != 0U &&
       config->tx_chunk != NULL && config->tx_chunk_size != 0U)
    {
        status = tx_queue_create(&serial->tx_queue,
                                 (CHAR *)config->name,
                                 TX_1_ULONG,
                                 config->tx_queue_storage,
                                 config->tx_queue_depth * sizeof(ULONG));
        if(status != TX_SUCCESS)
        {
            serial->diag.init_status = status;
            return status;
        }

        status = tx_semaphore_create(&serial->tx_done,
                                     (CHAR *)config->name,
                                     0U);
        if(status != TX_SUCCESS)
        {
            serial->diag.init_status = status;
            return status;
        }

        serial->tx_queue_enabled = 1U;
        if(bsp_uart_register_tx_callback(config->uart_port,
                                         app_serial_ldc_tx_done,
                                         serial) != 0)
        {
            serial->diag.init_status = TX_PTR_ERROR;
            return TX_PTR_ERROR;
        }
    }

    if(bsp_uart_register_rx_callback(config->uart_port,
                                     app_serial_ldc_rx,
                                     serial) != 0)
    {
        serial->diag.init_status = TX_PTR_ERROR;
        return TX_PTR_ERROR;
    }

    status = tx_thread_create(&serial->thread,
                              (CHAR *)config->name,
                              app_serial_ldc_thread_entry,
                              (ULONG)serial,
                              config->thread_stack,
                              config->thread_stack_size,
                              config->thread_priority,
                              config->thread_priority,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status != TX_SUCCESS)
    {
        serial->diag.init_status = status;
        return status;
    }

    serial->initialized = 1U;
    serial->diag.init_status = TX_SUCCESS;
    return TX_SUCCESS;
}

int app_serial_ldc_send_async(app_serial_ldc_t *serial,
                              const uint8_t *data,
                              uint16_t length)
{
    return app_serial_ldc_queue_bytes(serial, data, length);
}

int app_serial_ldc_send_blocking(app_serial_ldc_t *serial,
                                 const uint8_t *data,
                                 uint16_t length,
                                 uint32_t timeout_ms)
{
    if(serial == NULL || data == NULL || length == 0U)
        return -1;

    return bsp_uart_write(serial->uart_port, data, length, timeout_ms);
}

int app_serial_ldc_read_frame(app_serial_ldc_t *serial,
                              uint8_t *buffer,
                              uint16_t size)
{
    if(serial == NULL || buffer == NULL || size == 0U)
        return -1;

    return ldc_endpoint_read(&serial->endpoint, buffer, size);
}

ldc_endpoint_t *app_serial_ldc_endpoint(app_serial_ldc_t *serial)
{
    return (serial == NULL) ? NULL : &serial->endpoint;
}

const app_serial_ldc_diag_t *app_serial_ldc_diag(app_serial_ldc_t *serial)
{
    return (serial == NULL) ? NULL : &serial->diag;
}

bool app_serial_ldc_get_ldc_stats(app_serial_ldc_t *serial, ldc_stats_t *stats)
{
    if(serial == NULL)
        return false;

    return ldc_endpoint_get_stats(&serial->endpoint, stats);
}
