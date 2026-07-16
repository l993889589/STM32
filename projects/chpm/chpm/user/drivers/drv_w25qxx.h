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

typedef struct
{
    uint32_t ChipID;
    char ChipName[16];
    uint32_t TotalSize;
    uint16_t SectorSize;
} SFLASH_T;

extern SFLASH_T g_tSF;

sf_status_t sf_init(void);
sf_status_t sf_read(uint32_t address, void *data, size_t length);
sf_status_t sf_erase_sector_checked(uint32_t address);
sf_status_t sf_program(uint32_t address, const void *data, size_t length);
sf_status_t sf_verify(uint32_t address, const void *data, size_t length);
sf_status_t sf_last_status(void);

/* Compatibility entry points retained for existing CHPM application code. */
void bsp_InitSFlash(void);
uint32_t sf_ReadID(void);
void sf_EraseChip(void);
void sf_EraseSector(uint32_t address);
void sf_PageWrite(uint8_t *data, uint32_t address, uint16_t length);
uint8_t sf_WriteBuffer(uint8_t *data, uint32_t address, uint32_t length);
void sf_ReadBuffer(uint8_t *data, uint32_t address, uint32_t length);
void sf_ReadInfo(void);
void sfReadTest(void);

#endif
