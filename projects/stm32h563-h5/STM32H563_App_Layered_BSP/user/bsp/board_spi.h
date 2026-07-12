/**
 * @file board_spi.h
 * @brief Private board SPI interrupt-dispatch interface.
 */

#ifndef BOARD_SPI_H
#define BOARD_SPI_H

#include "bsp_spi.h"

/** @brief Dispatch one logical SPI vector from ISR context. */
void board_spi_irq_from_isr(board_spi_role_t role);
/** @brief Dispatch one logical SPI TX DMA vector from ISR context. */
void board_spi_tx_dma_irq_from_isr(board_spi_role_t role);

#endif /* BOARD_SPI_H */
