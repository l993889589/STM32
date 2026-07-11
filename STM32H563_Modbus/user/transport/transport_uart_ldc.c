/**
 * @file transport_uart_ldc.c
 * @brief UART-to-LDC byte transport adapter.
 */

#include "transport_uart_ldc.h"

#include <string.h>
#include "bsp_critical.h"

/**
 * @brief Enter the transport critical section and return its restore token.
 */
static uint32_t transport_uart_ldc_lock(void *argument)
{
    (void)argument;
    return bsp_critical_enter();
}

/**
 * @brief Restore the interrupt state saved by the transport lock.
 */
static void transport_uart_ldc_unlock(void *argument, uint32_t state)
{
    (void)argument;
    bsp_critical_exit(state);
}

/**
 * @brief Implement transport_uart_ldc_init() as documented by its interface contract.
 */
bsp_status_t transport_uart_ldc_init(transport_uart_ldc_t *transport,
                                     board_uart_role_t uart_role,
                                     const transport_uart_ldc_config_t *config)
{
    ldc_easy_config_t ldc_config;

    if((transport == NULL) || (config == NULL) ||
       (uart_role >= BOARD_UART_COUNT) || (config->ring_buffer == NULL) ||
       (config->packet_pool == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    memset(&ldc_config, 0, sizeof(ldc_config));
    ldc_config.ring_buffer = config->ring_buffer;
    ldc_config.ring_size = config->ring_size;
    ldc_config.packet_pool = config->packet_pool;
    ldc_config.packet_count = config->packet_count;
    ldc_config.max_frame = config->max_frame;
    ldc_config.timeout_us = config->timeout_us;
    ldc_config.delimiter_enabled = config->delimiter_enabled != 0U;
    ldc_config.delimiter = config->delimiter;
    ldc_config.mode = LDC_MODE_PROTECT;
    ldc_config.lock = transport_uart_ldc_lock;
    ldc_config.unlock = transport_uart_ldc_unlock;

    if(!ldc_easy_init(&transport->ldc, &ldc_config))
    {
        return BSP_STATUS_IO_ERROR;
    }

    transport->uart_role = uart_role;
    transport->is_initialized = 1U;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement transport_uart_ldc_step() as documented by its interface contract.
 */
bsp_status_t transport_uart_ldc_step(transport_uart_ldc_t *transport,
                                     uint32_t elapsed_us)
{
    uint32_t received = 0U;
    bsp_status_t status;

    if((transport == NULL) || (transport->is_initialized == 0U))
    {
        return BSP_STATUS_NOT_READY;
    }

    do
    {
        status = bsp_uart_try_read(transport->uart_role,
                                   transport->receive_chunk,
                                   sizeof(transport->receive_chunk),
                                   &received);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
        if((received > 0U) &&
           (ldc_easy_add(&transport->ldc, transport->receive_chunk, received) != received))
        {
            return BSP_STATUS_OVERFLOW;
        }
    } while(received == sizeof(transport->receive_chunk));

    if(elapsed_us > 0U)
    {
        ldc_easy_tick_us(&transport->ldc, elapsed_us);
    }
    return BSP_STATUS_OK;
}

/**
 * @brief Implement transport_uart_ldc_pop() as documented by its interface contract.
 */
int transport_uart_ldc_pop(transport_uart_ldc_t *transport,
                           uint8_t *data,
                           uint32_t capacity)
{
    if((transport == NULL) || (transport->is_initialized == 0U))
    {
        return -1;
    }
    return ldc_easy_pop(&transport->ldc, data, capacity);
}
