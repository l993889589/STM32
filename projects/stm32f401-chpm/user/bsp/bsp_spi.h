/**
 * @file bsp_spi.h
 * @brief Private-handle SPI1 bus interface for board devices.
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stddef.h>
#include <stdint.h>

#include "bsp_status.h"

/** @brief Initialize PA5/PA6/PA7 and SPI1 mode 0. */
bsp_status_t bsp_spi_init(void);

/** @brief Acquire the non-reentrant SPI1 bus. */
bsp_status_t bsp_spi_acquire(void);

/** @brief Release SPI1 after the device chip-select returns inactive. */
void bsp_spi_release(void);

/**
 * @brief Exchange bytes on an acquired bus.
 * @note A NULL TX pointer clocks 0xFF; a NULL RX pointer discards input.
 */
bsp_status_t bsp_spi_transfer(const uint8_t *tx_data,
                              uint8_t *rx_data,
                              size_t length,
                              uint32_t timeout_ms);

#endif /* BSP_SPI_H */
