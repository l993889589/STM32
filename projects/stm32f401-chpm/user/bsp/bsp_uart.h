/**
 * @file bsp_uart.h
 * @brief Logical USART1/USART2 interface with private STM32 resources.
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Logical serial links used by the product. */
typedef enum
{
    BSP_UART_MODBUS = 0,
    BSP_UART_DWIN,
    BSP_UART_COUNT
} bsp_uart_port_t;

/** @brief Receive event that ended one DMA publication. */
typedef enum
{
    BSP_UART_RX_EVENT_HALF_TRANSFER = 0,
    BSP_UART_RX_EVENT_FULL_TRANSFER,
    BSP_UART_RX_EVENT_IDLE
} bsp_uart_rx_event_t;

/** @brief Timestamp and segmentation metadata for one published DMA span. */
typedef struct
{
    uint32_t timestamp_cycles;
    uint16_t event_length;
    bsp_uart_rx_event_t event;
    uint8_t first_segment;
    uint8_t last_segment;
} bsp_uart_rx_info_t;

/** @brief Receive callback executed from UART or DMA interrupt context. */
typedef void (*bsp_uart_rx_callback_t)(const uint8_t *data,
                                       uint16_t length,
                                       const bsp_uart_rx_info_t *info,
                                       void *context);

/** @brief Receive recovery callback executed from UART interrupt context. */
typedef void (*bsp_uart_error_callback_t)(void *context);

/** @brief Per-port monotonic health counters. */
typedef struct
{
    uint32_t rx_events;
    uint32_t rx_bytes;
    uint32_t rx_errors;
    uint32_t rx_restarts;
    uint32_t rx_overflows;
    uint32_t tx_bytes;
    uint32_t tx_errors;
} bsp_uart_diagnostics_t;

/** @brief Initialize pins, clocks, UARTs, DMA streams, and interrupt vectors. */
bsp_status_t bsp_uart_init(void);

/** @brief Register the single receive owner for a logical port. */
bsp_status_t bsp_uart_set_callbacks(bsp_uart_port_t port,
                                    bsp_uart_rx_callback_t receive,
                                    bsp_uart_error_callback_t error,
                                    void *context);

/** @brief Start circular DMA receive-to-idle on both logical ports. */
bsp_status_t bsp_uart_start_rx(void);

/** @brief Transmit one complete buffer using a bounded polling wait. */
bsp_status_t bsp_uart_write(bsp_uart_port_t port,
                            const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms);

/** @brief Return the configured baud rate, or zero for an invalid port. */
uint32_t bsp_uart_baud_rate(bsp_uart_port_t port);

/** @brief Return the current producer position in the circular RX buffer. */
uint16_t bsp_uart_rx_dma_position(bsp_uart_port_t port);

/** @brief Copy one consistent health snapshot. */
bsp_status_t bsp_uart_get_diagnostics(bsp_uart_port_t port,
                                      bsp_uart_diagnostics_t *diagnostics);

#endif /* BSP_UART_H */
