/**
 * @file bsp_uart.h
 * @brief ART-Pi H750 UART BSP interface and event contract.
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

/*
 * Enable only the ports used by the current board. Pin and alternate-function
 * mappings are kept together at the top of bsp_uart.c.
 */
#define BSP_UART1_ENABLED              0
#define BSP_UART2_ENABLED              0
#define BSP_UART3_ENABLED              0
#define BSP_UART4_ENABLED              1
#define BSP_UART5_ENABLED              1
#define BSP_UART6_ENABLED              0
#define BSP_UART7_ENABLED              0
#define BSP_UART8_ENABLED              0

#define BSP_UART1_BAUD_RATE       115200U
#define BSP_UART2_BAUD_RATE       115200U
#define BSP_UART3_BAUD_RATE       115200U
#define BSP_UART4_BAUD_RATE       115200U
#define BSP_UART5_BAUD_RATE       115200U
#define BSP_UART6_BAUD_RATE       115200U
#define BSP_UART7_BAUD_RATE       115200U
#define BSP_UART8_BAUD_RATE       115200U

#define BSP_UART1_TX_BUFFER_SIZE    1024U
#define BSP_UART2_TX_BUFFER_SIZE    1024U
#define BSP_UART3_TX_BUFFER_SIZE    1024U
#define BSP_UART4_TX_BUFFER_SIZE    1024U
#define BSP_UART5_TX_BUFFER_SIZE    1024U
#define BSP_UART6_TX_BUFFER_SIZE    1024U
#define BSP_UART7_TX_BUFFER_SIZE    1024U
#define BSP_UART8_TX_BUFFER_SIZE    1024U

typedef enum
{
    BSP_UART_PORT_1 = 0,
    BSP_UART_PORT_2,
    BSP_UART_PORT_3,
    BSP_UART_PORT_4,
    BSP_UART_PORT_5,
    BSP_UART_PORT_6,
    BSP_UART_PORT_7,
    BSP_UART_PORT_8,
    BSP_UART_PORT_COUNT
} bsp_uart_port_t;

#define BSP_UART_DEBUG BSP_UART_PORT_4

#define BSP_UART_RX_EVENT_DATA          (1UL << 0)
#define BSP_UART_RX_EVENT_PARITY_ERROR  (1UL << 1)
#define BSP_UART_RX_EVENT_FRAMING_ERROR (1UL << 2)
#define BSP_UART_RX_EVENT_NOISE_ERROR   (1UL << 3)
#define BSP_UART_RX_EVENT_OVERRUN       (1UL << 4)
#define BSP_UART_RX_EVENT_ERROR_MASK    \
    (BSP_UART_RX_EVENT_PARITY_ERROR | BSP_UART_RX_EVENT_FRAMING_ERROR | \
     BSP_UART_RX_EVENT_NOISE_ERROR | BSP_UART_RX_EVENT_OVERRUN)

/*
 * IRQ reception currently calls this function with length == 1. The timestamp
 * is the raw DWT cycle value captured immediately after RDR is read and marks
 * the completed character as closely as this software path permits. A future
 * block backend would provide only the final byte timestamp, so protocols that
 * require per-byte gaps must reject or separately timestamp length > 1.
 */
typedef void (*bsp_uart_rx_callback_t)(const uint8_t *data,
                                       uint16_t length,
                                       uint32_t end_timestamp_ticks,
                                       uint32_t event_flags,
                                       void *argument);
typedef void (*bsp_uart_tx_callback_t)(void *argument);
typedef void (*bsp_uart_baud_sync_callback_t)(uint32_t active_baud_rate,
                                               void *argument);

void bsp_uart_init(void);
HAL_StatusTypeDef bsp_uart_receive_start(bsp_uart_port_t port,
                                         bsp_uart_rx_callback_t callback,
                                         void *argument);
HAL_StatusTypeDef bsp_uart_receive_stop(bsp_uart_port_t port);
/*
 * Return nonzero only when no character is being received and no receive/error
 * event is waiting in the UART. Call while the corresponding RX callback path
 * is serialized when using this as a protocol frame-boundary guard.
 */
uint8_t bsp_uart_receive_quiescent(bsp_uart_port_t port);
HAL_StatusTypeDef bsp_uart_set_baud_rate(bsp_uart_port_t port,
                                          uint32_t baud_rate);
/*
 * Reconfigure hardware while only this UART IRQ is masked. The optional,
 * bounded callback runs after hardware success/rollback and before reception
 * resumes, allowing a protocol receiver to update timing atomically. Call
 * this API only from thread/main context, never from an ISR.
 */
HAL_StatusTypeDef bsp_uart_set_baud_rate_synchronized(
    bsp_uart_port_t port,
    uint32_t baud_rate,
    bsp_uart_baud_sync_callback_t sync_callback,
    void *argument);
uint32_t bsp_uart_get_baud_rate(bsp_uart_port_t port);
HAL_StatusTypeDef bsp_uart_set_tx_callbacks(bsp_uart_port_t port,
                                             bsp_uart_tx_callback_t send_before,
                                             bsp_uart_tx_callback_t send_complete,
                                             void *argument);
size_t bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, size_t length);
size_t bsp_uart_write_string(bsp_uart_port_t port, const char *text);
uint8_t bsp_uart_tx_empty(bsp_uart_port_t port);

#endif
