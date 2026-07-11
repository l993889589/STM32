/**
 * @file bsp_spi.h
 * @brief Logical board SPI public interface.
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdint.h>
#include "bsp_status.h"

typedef enum
{
    BOARD_SPI_FLASH = 0,
    BOARD_SPI_DISPLAY,
    BOARD_SPI_COUNT
} board_spi_role_t;

typedef enum
{
    BSP_SPI_CLOCK_POLARITY_LOW = 0,
    BSP_SPI_CLOCK_POLARITY_HIGH
} bsp_spi_clock_polarity_t;

typedef enum
{
    BSP_SPI_CLOCK_PHASE_FIRST_EDGE = 0,
    BSP_SPI_CLOCK_PHASE_SECOND_EDGE
} bsp_spi_clock_phase_t;

typedef struct
{
    uint32_t baud_rate_hz;
    bsp_spi_clock_polarity_t clock_polarity;
    bsp_spi_clock_phase_t clock_phase;
} bsp_spi_config_t;

/**
 * Initialize a logical board SPI bus.
 * @param role Logical SPI bus role.
 * @param config Requested clock in hertz and SPI mode.
 * @return BSP status.
 */
bsp_status_t bsp_spi_init(board_spi_role_t role, const bsp_spi_config_t *config);
/**
 * Read the physical SPI clock selected by the prescaler solver.
 * @param role Logical SPI role.
 * @param achieved_baud_rate_hz Receives the actual bus clock in hertz.
 * @return BSP status.
 */
bsp_status_t bsp_spi_get_achieved_baud_rate(
    board_spi_role_t role,
    uint32_t *achieved_baud_rate_hz);
/**
 * Assert or release the role's board-owned chip select.
 * @param role Logical SPI device/bus role.
 * @param is_selected Nonzero to assert chip select; zero to release it.
 * @return BSP status.
 */
bsp_status_t bsp_spi_select(board_spi_role_t role, uint8_t is_selected);
/**
 * Write a bounded byte sequence on a logical SPI bus.
 * @param role Logical SPI role.
 * @param data Caller-owned transmit bytes valid for the duration of the call.
 * @param length Number of bytes to write.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status, including timeout and I/O errors.
 */
bsp_status_t bsp_spi_write(board_spi_role_t role,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);
/**
 * Read a bounded byte sequence from a logical SPI bus.
 * @param role Logical SPI role.
 * @param data Caller-owned destination buffer.
 * @param length Number of bytes to read.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status, including timeout and I/O errors.
 */
bsp_status_t bsp_spi_read(board_spi_role_t role,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);

#endif
