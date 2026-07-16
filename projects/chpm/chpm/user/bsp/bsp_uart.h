/**
 * @file bsp_uart.h
 * @brief Public UART ports, RX event metadata, callbacks and diagnostics.
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"
#include "stm32f4xx_hal.h"

typedef enum
{
    BSP_UART_MODBUS = 0,
    BSP_UART_DWIN,
    BSP_UART_COUNT
} bsp_uart_port_t;

typedef enum
{
    BSP_UART_RX_EVENT_IDLE = 0,
    BSP_UART_RX_EVENT_TRANSFER_COMPLETE
} bsp_uart_rx_event_t;

typedef struct
{
    uint32_t timestamp_cycles;
    uint16_t event_length;
    bool first_segment;
    bool last_segment;
    bsp_uart_rx_event_t event;
} bsp_uart_rx_info_t;

typedef struct
{
    volatile uint32_t uart1_errors;
    volatile uint32_t uart2_errors;
    volatile uint32_t uart1_restarts;
    volatile uint32_t uart2_restarts;
    volatile uint32_t uart1_restart_failures;
    volatile uint32_t uart2_restart_failures;
    volatile uint32_t uart1_rx_events;
    volatile uint32_t uart2_rx_events;
    volatile uint32_t uart1_rx_bytes;
    volatile uint32_t uart2_rx_bytes;
    volatile uint32_t uart1_ore_errors;
    volatile uint32_t uart2_ore_errors;
    volatile uint32_t uart1_dma_errors;
    volatile uint32_t uart2_dma_errors;
} bsp_uart_diagnostics_t;

typedef void (*bsp_uart_rx_callback_t)(const uint8_t *data,
                                       uint16_t length,
                                       const bsp_uart_rx_info_t *info,
                                       void *context);
typedef void (*bsp_uart_error_callback_t)(void *context);

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart2_rx;

/** @brief Initialize both board UARTs and their circular RX DMA channels. */
bsp_status_t bsp_uart_init(void);
/** @brief Bind protocol-owned callbacks to one UART port. */
bsp_status_t bsp_uart_set_callbacks(bsp_uart_port_t port,
                                    bsp_uart_rx_callback_t rx_callback,
                                    bsp_uart_error_callback_t error_callback,
                                    void *context);
/** @brief Start ReceiveToIdle DMA on all initialized UART ports. */
bsp_status_t bsp_uart_start_rx(void);
/** @brief Transmit one complete bounded frame on the selected UART. */
bsp_status_t bsp_uart_write(bsp_uart_port_t port,
                            const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms);
/** @brief Return the active baud rate for one UART port. */
uint32_t bsp_uart_baud_rate(bsp_uart_port_t port);
/** @brief Return the current circular RX DMA producer position. */
uint16_t bsp_uart_rx_dma_position(bsp_uart_port_t port);
/** @brief Return live UART and DMA diagnostic counters. */
const bsp_uart_diagnostics_t *bsp_uart_diagnostics(void);

#endif
