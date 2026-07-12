/**
 * @file mcu_spi.h
 * @brief Private STM32H5 SPI, DMA, IRQ, and callback-routing interface.
 */

#ifndef MCU_SPI_H
#define MCU_SPI_H

#include <stdbool.h>

#include "bsp_spi.h"
#include "stm32h5xx_hal.h"

/** @brief Static mutable state owned by one logical SPI role. */
typedef struct
{
    SPI_HandleTypeDef handle;
    DMA_HandleTypeDef tx_dma;
    board_spi_role_t role;
    uint32_t achieved_baud_rate_hz;
    bool is_initialized;
    bool tx_dma_initialized;
    bsp_spi_tx_cb_t tx_callback;
    void *tx_argument;
} mcu_spi_context_t;

/** @brief Initialize one STM32H5 SPI instance. */
bsp_status_t mcu_spi_init(mcu_spi_context_t *context,
                          board_spi_role_t role,
                          SPI_TypeDef *instance,
                          uint32_t kernel_clock_hz,
                          const bsp_spi_config_t *config);
/** @brief Configure and link one context-owned GPDMA TX channel. */
bsp_status_t mcu_spi_configure_tx_dma(mcu_spi_context_t *context,
                                      DMA_Channel_TypeDef *instance,
                                      uint32_t request);
/** @brief Execute bounded blocking SPI transmit. */
bsp_status_t mcu_spi_write(mcu_spi_context_t *context,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);
/** @brief Execute bounded blocking SPI receive. */
bsp_status_t mcu_spi_read(mcu_spi_context_t *context,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);
/** @brief Execute bounded blocking full-duplex SPI exchange. */
bsp_status_t mcu_spi_transfer(mcu_spi_context_t *context,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms);
/** @brief Start asynchronous context-owned TX DMA. */
bsp_status_t mcu_spi_write_dma(mcu_spi_context_t *context,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument);
/** @brief Abort one initialized SPI context. */
bsp_status_t mcu_spi_abort(mcu_spi_context_t *context);
/** @brief Dispatch one SPI vector from ISR context. */
void mcu_spi_irq_from_isr(mcu_spi_context_t *context);
/** @brief Dispatch one SPI TX DMA vector from ISR context. */
void mcu_spi_tx_dma_irq_from_isr(mcu_spi_context_t *context);

#endif /* MCU_SPI_H */
