/**
 * @file bsp_w25q128.h
 * @brief ART-Pi H750 SPI W25Q128 BSP interface.
 */

#ifndef BSP_W25Q128_H
#define BSP_W25Q128_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BSP_W25Q128_JEDEC_ID       0xEF4018UL
#define BSP_W25Q128_TOTAL_SIZE     (16UL * 1024UL * 1024UL)
#define BSP_W25Q128_SECTOR_SIZE    4096UL
#define BSP_W25Q128_PAGE_SIZE      256UL

HAL_StatusTypeDef bsp_w25q128_init(void);
HAL_StatusTypeDef bsp_w25q128_read_id(uint32_t *jedec_id);
HAL_StatusTypeDef bsp_w25q128_read(uint32_t address,
                                   uint8_t *data,
                                   size_t length);
HAL_StatusTypeDef bsp_w25q128_write(uint32_t address,
                                    const uint8_t *data,
                                    size_t length);
HAL_StatusTypeDef bsp_w25q128_erase_sector(uint32_t address);
HAL_StatusTypeDef bsp_w25q128_read_status(uint8_t *status_register);

#endif
