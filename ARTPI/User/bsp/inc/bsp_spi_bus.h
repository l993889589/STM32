/**
 * @file bsp_spi_bus.h
 * @brief ART-Pi H750 shared SPI bus BSP interface.
 */

#ifndef BSP_SPI_BUS_H
#define BSP_SPI_BUS_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BSP_SPI_TRANSFER_MODE_POLLING     0U
#define BSP_SPI_TRANSFER_MODE_INTERRUPT   1U
#define BSP_SPI_TRANSFER_MODE_DMA         2U

/* Select one backend. DMA is the default for the ART-Pi W25Q128 bus. */
#ifndef BSP_SPI_TRANSFER_MODE
#define BSP_SPI_TRANSFER_MODE BSP_SPI_TRANSFER_MODE_DMA
#endif

#if (BSP_SPI_TRANSFER_MODE != BSP_SPI_TRANSFER_MODE_POLLING) && \
    (BSP_SPI_TRANSFER_MODE != BSP_SPI_TRANSFER_MODE_INTERRUPT) && \
    (BSP_SPI_TRANSFER_MODE != BSP_SPI_TRANSFER_MODE_DMA)
#error "Invalid BSP_SPI_TRANSFER_MODE"
#endif

#define BSP_SPI_BUFFER_SIZE         4096U
#define BSP_SPI_DUMMY_BYTE          0xFFU
#define BSP_SPI_DEFAULT_TIMEOUT_MS   100U

HAL_StatusTypeDef bsp_spi_bus_init(void);
HAL_StatusTypeDef bsp_spi_bus_config(uint32_t baud_rate_prescaler,
                                     uint32_t clock_phase,
                                     uint32_t clock_polarity);
HAL_StatusTypeDef bsp_spi_bus_enter(void);
void bsp_spi_bus_exit(void);
uint8_t bsp_spi_bus_busy(void);
HAL_StatusTypeDef bsp_spi_bus_transfer(const uint8_t *tx_data,
                                       uint8_t *rx_data,
                                       size_t length,
                                       uint32_t timeout_ms);
uint32_t bsp_spi_bus_get_transfer_mode(void);

#endif
