/**
 * @file transport_uart_ldc.h
 * @brief UART-to-LDC transport configuration and context interface.
 */

#ifndef TRANSPORT_UART_LDC_H
#define TRANSPORT_UART_LDC_H

#include <stdint.h>
#include "bsp_status.h"
#include "bsp_uart.h"
#include "ldc_easy.h"

typedef struct
{
    board_uart_role_t uart_role;
    ldc_easy_t ldc;
    uint8_t receive_chunk[64];
    uint8_t is_initialized;
} transport_uart_ldc_t;

typedef struct
{
    uint8_t *ring_buffer;
    uint32_t ring_size;
    ldc_packet_t *packet_pool;
    uint16_t packet_count;
    uint32_t max_frame;
    uint32_t timeout_us;
    uint8_t delimiter;
    uint8_t delimiter_enabled;
} transport_uart_ldc_config_t;

/**
 * Initialize a UART-to-LDC transport using caller-owned static storage.
 * @param transport Mutable transport context.
 * @param uart_role Logical UART source.
 * @param config LDC ring, packet pool, framing, and timeout configuration.
 * @return BSP status.
 */
bsp_status_t transport_uart_ldc_init(transport_uart_ldc_t *transport,
                                     board_uart_role_t uart_role,
                                     const transport_uart_ldc_config_t *config);
/**
 * Advance UART reception and LDC framing without unbounded work.
 * @param transport Initialized transport context.
 * @param elapsed_us Elapsed service time in microseconds.
 * @return BSP status.
 */
bsp_status_t transport_uart_ldc_step(transport_uart_ldc_t *transport,
                                     uint32_t elapsed_us);
/**
 * Pop one completed LDC frame into caller storage.
 * @param transport Initialized transport context.
 * @param data Destination buffer.
 * @param capacity Destination capacity in bytes.
 * @return Frame length, zero when empty, or a negative error from LDC.
 */
int transport_uart_ldc_pop(transport_uart_ldc_t *transport,
                           uint8_t *data,
                           uint32_t capacity);

#endif
