#ifndef BSP_QSPI_W25Q128_H
#define BSP_QSPI_W25Q128_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BSP_QSPI_W25Q128_BASE_ADDRESS  0x90000000UL
#define BSP_QSPI_W25Q128_JEDEC_ID      0xEF4017UL
#define BSP_QSPI_W25Q128_TOTAL_SIZE    (8UL * 1024UL * 1024UL)
#define BSP_QSPI_W25Q128_SECTOR_SIZE   4096UL
#define BSP_QSPI_W25Q128_PAGE_SIZE     256UL

HAL_StatusTypeDef bsp_qspi_w25q128_init(void);
HAL_StatusTypeDef bsp_qspi_w25q128_read_id(uint32_t *jedec_id);
HAL_StatusTypeDef bsp_qspi_w25q128_read(uint32_t address,
                                        uint8_t *data,
                                        size_t length);
HAL_StatusTypeDef bsp_qspi_w25q128_program(uint32_t address,
                                           const uint8_t *data,
                                           size_t length);
HAL_StatusTypeDef bsp_qspi_w25q128_erase_sector(uint32_t address);
HAL_StatusTypeDef bsp_qspi_w25q128_enter_memory_mapped(void);
HAL_StatusTypeDef bsp_qspi_w25q128_leave_memory_mapped(void);

#endif
