#ifndef BSP_QSPI_FLASH_H
#define BSP_QSPI_FLASH_H

#include <stdint.h>

typedef struct
{
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity_id;
    uint8_t read_mode;
    uint32_t capacity_bytes;
} bsp_qspi_flash_id_t;

int bsp_qspi_flash_read_id(bsp_qspi_flash_id_t *id);
int bsp_qspi_flash_read(uint32_t address, uint8_t *data, uint32_t length);
int bsp_qspi_flash_erase(uint32_t address, uint32_t length);
int bsp_qspi_flash_write(uint32_t address, const uint8_t *data, uint32_t length);
uint32_t bsp_qspi_flash_crc32(uint32_t address, uint32_t length);

#endif /* BSP_QSPI_FLASH_H */
