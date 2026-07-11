/**
 * @file bsp_spi_stm32h5.h
 * @brief Private STM32H5 SPI context and binding interface.
 */

#ifndef BSP_SPI_STM32H5_H
#define BSP_SPI_STM32H5_H

#include <stdbool.h>
#include "bsp_spi.h"
#include "stm32h5xx_hal.h"

typedef struct
{
    SPI_HandleTypeDef handle;
    uint32_t achieved_baud_rate_hz;
    bool is_initialized;
} bsp_spi_stm32h5_context_t;

/**
 * Initialize one STM32H5 SPI instance.
 * @param context Static SPI context owned by the board role.
 * @param instance STM32 SPI peripheral instance.
 * @param kernel_clock_hz Actual SPI kernel frequency in hertz.
 * @param config Requested bus configuration.
 * @return BSP status.
 */
bsp_status_t bsp_spi_stm32h5_init(bsp_spi_stm32h5_context_t *context,
                                  SPI_TypeDef *instance,
                                  uint32_t kernel_clock_hz,
                                  const bsp_spi_config_t *config);
/**
 * Write bytes through an initialized STM32H5 SPI context.
 * @param context Initialized SPI context.
 * @param data Transmit buffer valid until the call returns.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_spi_stm32h5_write(bsp_spi_stm32h5_context_t *context,
                                   const uint8_t *data,
                                   uint32_t length,
                                   uint32_t timeout_ms);
/**
 * Read bytes through an initialized STM32H5 SPI context.
 * @param context Initialized SPI context.
 * @param data Destination buffer.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_spi_stm32h5_read(bsp_spi_stm32h5_context_t *context,
                                  uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms);

#endif
