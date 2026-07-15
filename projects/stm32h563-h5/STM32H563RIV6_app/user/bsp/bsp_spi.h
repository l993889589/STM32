/**
 * @file bsp_spi.h
 * @brief Logical board SPI interface with physical clock requests.
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Logical SPI bus/device roles. */
typedef enum
{
    BOARD_SPI_FLASH = 0,
    BOARD_SPI_DISPLAY,
    BOARD_SPI_COUNT
} bsp_spi_role_t;

/** @brief SPI clock idle polarity. */
typedef enum
{
    BSP_SPI_CLOCK_POLARITY_LOW = 0,
    BSP_SPI_CLOCK_POLARITY_HIGH
} bsp_spi_clock_polarity_t;

/** @brief SPI sampling edge selection. */
typedef enum
{
    BSP_SPI_CLOCK_PHASE_FIRST_EDGE = 0,
    BSP_SPI_CLOCK_PHASE_SECOND_EDGE
} bsp_spi_clock_phase_t;

/** @brief Requested maximum SPI clock and mode. */
typedef struct
{
    uint32_t baud_rate_hz;
    bsp_spi_clock_polarity_t clock_polarity;
    bsp_spi_clock_phase_t clock_phase;
} bsp_spi_config_t;

/** @brief Asynchronous transmit completion callback executed in ISR context. */
typedef void (*bsp_spi_tx_cb_t)(bsp_spi_role_t role,
                                bsp_status_t status,
                                void *argument);

/**
 * @brief Initialize a logical SPI role without exceeding the requested clock.
 * @param role Logical SPI role.
 * @param config Requested maximum clock and SPI mode.
 * @return BSP status.
 */
bsp_status_t bsp_spi_init(bsp_spi_role_t role, const bsp_spi_config_t *config);

/**
 * @brief Read the achieved SPI clock.
 * @param role Logical SPI role.
 * @param achieved_baud_rate_hz Destination in hertz.
 * @return BSP status.
 */
bsp_status_t bsp_spi_get_achieved_baud_rate(bsp_spi_role_t role,
                                            uint32_t *achieved_baud_rate_hz);

/**
 * @brief Assert or release the board-owned chip select.
 * @param role Logical SPI role.
 * @param is_selected Nonzero asserts active-low chip select.
 * @return BSP status.
 */
bsp_status_t bsp_spi_select(bsp_spi_role_t role, uint8_t is_selected);

/** @brief Write bytes with a bounded timeout. */
bsp_status_t bsp_spi_write(bsp_spi_role_t role,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);
/** @brief Read bytes with a bounded timeout. */
bsp_status_t bsp_spi_read(bsp_spi_role_t role,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);
/** @brief Exchange bytes full-duplex with a bounded timeout. */
bsp_status_t bsp_spi_transfer(bsp_spi_role_t role,
                              const uint8_t *tx_data,
                              uint8_t *rx_data,
                              uint32_t length,
                              uint32_t timeout_ms);

/**
 * @brief Start an asynchronous DMA transmit.
 * @param role Logical SPI role with a configured TX DMA channel.
 * @param data Static/aligned transmit buffer valid until callback.
 * @param length Byte count.
 * @param callback Optional ISR-context completion callback.
 * @param argument Callback argument with transfer lifetime.
 * @return BSP status.
 */
bsp_status_t bsp_spi_write_dma(bsp_spi_role_t role,
                               const uint8_t *data,
                               uint32_t length,
                               bsp_spi_tx_cb_t callback,
                               void *argument);

/** @brief Abort an active transfer and return the role to ready state. */
bsp_status_t bsp_spi_abort(bsp_spi_role_t role);

/** @brief Forward one SPI peripheral interrupt to the selected BSP bus. */
void bsp_spi_irq_from_isr(bsp_spi_role_t role);

/** @brief Forward one SPI transmit-DMA interrupt to the selected BSP bus. */
void bsp_spi_tx_dma_irq_from_isr(bsp_spi_role_t role);

#endif /* BSP_SPI_H */
