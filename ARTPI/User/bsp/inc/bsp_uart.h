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

/*
 * IRQ reception currently calls this function with length == 1. A future DMA
 * backend may pass a whole block without changing the application interface.
 * The data pointer is valid only for the duration of the callback.
 */
typedef void (*bsp_uart_rx_callback_t)(const uint8_t *data,
                                       uint16_t length,
                                       void *argument);
typedef void (*bsp_uart_tx_callback_t)(void *argument);

void bsp_uart_init(void);
HAL_StatusTypeDef bsp_uart_receive_start(bsp_uart_port_t port,
                                         bsp_uart_rx_callback_t callback,
                                         void *argument);
HAL_StatusTypeDef bsp_uart_receive_stop(bsp_uart_port_t port);
HAL_StatusTypeDef bsp_uart_set_tx_callbacks(bsp_uart_port_t port,
                                             bsp_uart_tx_callback_t send_before,
                                             bsp_uart_tx_callback_t send_complete,
                                             void *argument);
size_t bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, size_t length);
size_t bsp_uart_write_string(bsp_uart_port_t port, const char *text);
uint8_t bsp_uart_tx_empty(bsp_uart_port_t port);

#endif
