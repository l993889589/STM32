#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stddef.h>
#include <stdint.h>

#include "bsp_uart.h"

#define BSP_RS485_UART_PORT BSP_UART_PORT_5

HAL_StatusTypeDef bsp_rs485_init(void);
HAL_StatusTypeDef bsp_rs485_receive_start(bsp_uart_rx_callback_t callback,
                                           void *argument);
HAL_StatusTypeDef bsp_rs485_receive_stop(void);
size_t bsp_rs485_write(const uint8_t *data, size_t length);
uint8_t bsp_rs485_tx_empty(void);

#endif
