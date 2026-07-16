/**
 * @file bsp_flash.h
 * @brief BSP_FLASH SPI NOR compatibility API used by storage services.
 */

#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_FLASH_FLASH_SIZE_BYTES      (16UL * 1024UL * 1024UL)
#define BSP_FLASH_SECTOR_SIZE_BYTES     4096UL
#define BSP_FLASH_PAGE_SIZE_BYTES       256UL

typedef struct
{
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
} bsp_flash_id_t;

/** @brief Initialize the driver after the board SPI role is ready. */
bool bsp_flash_init(void);

/**
 * @brief Read the JEDEC ID.
 * @return false when the SPI bus is not bound or the transfer fails.
 */
bool bsp_flash_read_id(bsp_flash_id_t *id);

/**
 * @brief Read bytes from a byte address inside the 16 MiB device.
 *
 * chunks and retries reads internally; callers still need to serialize access
 * at the service level if multiple tasks use the same SPI bus.
 */
bool bsp_flash_read(uint32_t address, uint8_t *data, uint32_t len);

/** @brief Erase one 4 KiB sector; address must be sector aligned. */
bool bsp_flash_erase_4k(uint32_t address);

/**
 * @brief Program one page fragment.
 *
 * The call must not cross a 256-byte page boundary
 * and the destination bytes must already be erased.
 */
bool bsp_flash_page_program(uint32_t address, const uint8_t *data, uint32_t len);

/** @brief Program a byte range by splitting it into page-program calls. */
bool bsp_flash_write(uint32_t address, const uint8_t *data, uint32_t len);

/** @brief Read back a range and compare it with expected bytes. */
bool bsp_flash_read_verify(uint32_t address, const uint8_t *expected, uint32_t len);

#endif /* BSP_FLASH_H */
