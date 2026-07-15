#ifndef BOOT_SPI_FLASH_H
#define BOOT_SPI_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

HAL_StatusTypeDef boot_spi_flash_init(void);
HAL_StatusTypeDef boot_spi_flash_read(uint32_t address,
                                      uint8_t *data,
                                      size_t length);
HAL_StatusTypeDef boot_spi_flash_program(uint32_t address,
                                         const uint8_t *data,
                                         size_t length);

#endif
