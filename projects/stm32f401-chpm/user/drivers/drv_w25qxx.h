/**
 * @file drv_w25qxx.h
 * @brief W25Q64 checked read, erase, program, verify, and wait-hook API.
 */

#ifndef DRV_W25QXX_H
#define DRV_W25QXX_H

#include <stddef.h>
#include <stdint.h>

#define W25Q64_JEDEC_ID          0xEF4017UL
#define W25Q64_TOTAL_SIZE        (8UL * 1024UL * 1024UL)
#define W25Q64_SECTOR_SIZE       4096UL
#define W25Q64_PAGE_SIZE         256UL

typedef enum
{
    SF_STATUS_OK = 0,
    SF_STATUS_INVALID_ARGUMENT,
    SF_STATUS_NOT_READY,
    SF_STATUS_IO_ERROR,
    SF_STATUS_TIMEOUT,
    SF_STATUS_VERIFY_FAILED
} sf_status_t;

/** @brief Optional callback invoked between device-busy status polls. */
typedef void (*sf_wait_hook_t)(void);

typedef struct
{
    uint32_t ChipID;
    char ChipName[16];
    uint32_t TotalSize;
    uint16_t SectorSize;
} SFLASH_T;

extern SFLASH_T g_tSF;

/** @brief Initialize the flash device and validate its JEDEC identity. */
sf_status_t sf_init(void);

/** @brief Read a bounded byte range from flash. */
sf_status_t sf_read(uint32_t address, void *data, size_t length);

/** @brief Erase and verify one sector containing @p address. */
sf_status_t sf_erase_sector_checked(uint32_t address);

/** @brief Program a bounded byte range using page-sized operations. */
sf_status_t sf_program(uint32_t address, const void *data, size_t length);

/** @brief Verify a flash range against caller-provided bytes. */
sf_status_t sf_verify(uint32_t address, const void *data, size_t length);

/** @brief Return the most recent compatibility-API status. */
sf_status_t sf_last_status(void);

/** @brief Install an optional scheduler-yield hook for busy polling. */
void sf_set_wait_hook(sf_wait_hook_t hook);

/* Compatibility entry points retained for existing CHPM application code. */
/** @brief Compatibility initializer for existing application code. */
void bsp_InitSFlash(void);

/** @brief Compatibility JEDEC-ID read. */
uint32_t sf_ReadID(void);

/** @brief Compatibility full-chip erase. */
void sf_EraseChip(void);

/** @brief Compatibility sector erase. */
void sf_EraseSector(uint32_t address);

/** @brief Compatibility single-page program. */
void sf_PageWrite(uint8_t *data, uint32_t address, uint16_t length);

/** @brief Compatibility buffer program. */
uint8_t sf_WriteBuffer(uint8_t *data, uint32_t address, uint32_t length);

/** @brief Compatibility buffer read. */
void sf_ReadBuffer(uint8_t *data, uint32_t address, uint32_t length);

/** @brief Compatibility information hook. */
void sf_ReadInfo(void);

/** @brief Compatibility flash self-test hook. */
void sfReadTest(void);

#endif /* DRV_W25QXX_H */
