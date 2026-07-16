/**
 * @file gd25lq128.h
 * @brief Bounded GD25LQ128 SPI NOR identification, read, program, and erase interface.
 */

#ifndef GD25LQ128_H
#define GD25LQ128_H

#include <stdint.h>

#include "bsp_status.h"

#define GD25LQ128_CAPACITY_BYTES (16UL * 1024UL * 1024UL)
#define GD25LQ128_PAGE_BYTES     (256UL)
#define GD25LQ128_SECTOR_BYTES   (4096UL)

typedef struct
{
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
} gd25lq128_id_t;

/**
 * @brief Initialize SPI1 and verify that a plausible 128-Mbit JEDEC device responds.
 * @return BSP status.
 */
bsp_status_t gd25lq128_init(void);

/**
 * @brief Read the JEDEC manufacturer, memory type, and capacity bytes.
 * @param identifier Receives the three-byte JEDEC identifier.
 * @return BSP status.
 */
bsp_status_t gd25lq128_read_id(gd25lq128_id_t *identifier);

/**
 * @brief Read a bounded range from SPI NOR.
 * @param address Zero-based 24-bit flash address.
 * @param data Caller-owned destination buffer.
 * @param length Number of bytes to read.
 * @param timeout_ms Maximum time for each bounded SPI chunk.
 * @return BSP status.
 */
bsp_status_t gd25lq128_read(uint32_t address,
                                uint8_t *data,
                                uint32_t length,
                                uint32_t timeout_ms);

/**
 * @brief Program bytes, automatically splitting writes at 256-byte page boundaries.
 * @param address Zero-based flash address.
 * @param data Source bytes valid until the function returns.
 * @param length Number of bytes to program.
 * @param timeout_ms Maximum total device-busy wait per page.
 * @return BSP status.
 * @note The destination must already be erased; programming can only clear bits.
 */
bsp_status_t gd25lq128_program(uint32_t address,
                                   const uint8_t *data,
                                   uint32_t length,
                                   uint32_t timeout_ms);

/**
 * @brief Erase one aligned 4-KiB sector with a bounded busy wait.
 * @param address Any address within the sector; the driver aligns it down.
 * @param timeout_ms Maximum erase completion time.
 * @return BSP status.
 */
bsp_status_t gd25lq128_erase_sector(uint32_t address,
                                        uint32_t timeout_ms);

#endif
