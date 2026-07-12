/**
 * @file board_uart.h
 * @brief Private board UART interrupt-dispatch interface.
 */

#ifndef BOARD_UART_H
#define BOARD_UART_H

#include "bsp_uart.h"

/**
 * @brief Dispatch a board UART interrupt by logical role.
 * @param port Logical UART role owning the active vector.
 * @note ISR context only.
 */
void board_uart_irq_from_isr(bsp_uart_port_t port);

/**
 * @brief Dispatch a board UART receive-DMA interrupt by logical role.
 * @param port Logical UART role owning the active DMA vector.
 * @note ISR context only.
 */
void board_uart_rx_dma_irq_from_isr(bsp_uart_port_t port);

#endif /* BOARD_UART_H */
