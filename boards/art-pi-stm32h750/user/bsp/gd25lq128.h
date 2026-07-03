#ifndef GD25LQ128_H
#define GD25LQ128_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define GD25LQ128_FLASH_SIZE_BYTES      (16UL * 1024UL * 1024UL)
#define GD25LQ128_SECTOR_SIZE_BYTES     4096UL
#define GD25LQ128_PAGE_SIZE_BYTES       256UL

typedef struct
{
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
} gd25lq128_id_t;

bool gd25lq128_bind(SPI_HandleTypeDef *handle);
bool gd25lq128_read_id(gd25lq128_id_t *id);
bool gd25lq128_read(uint32_t address, uint8_t *data, uint32_t len);
bool gd25lq128_erase_4k(uint32_t address);
bool gd25lq128_page_program(uint32_t address, const uint8_t *data, uint32_t len);
bool gd25lq128_write(uint32_t address, const uint8_t *data, uint32_t len);
bool gd25lq128_read_verify(uint32_t address, const uint8_t *expected, uint32_t len);

#endif /* GD25LQ128_H */
