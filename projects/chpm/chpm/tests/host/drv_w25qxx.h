#ifndef TEST_DRV_W25QXX_H
#define TEST_DRV_W25QXX_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SF_STATUS_OK = 0,
    SF_STATUS_INVALID_ARGUMENT,
    SF_STATUS_NOT_READY,
    SF_STATUS_IO_ERROR,
    SF_STATUS_TIMEOUT,
    SF_STATUS_VERIFY_FAILED
} sf_status_t;

sf_status_t sf_read(uint32_t address, void *data, size_t length);
sf_status_t sf_erase_sector_checked(uint32_t address);
sf_status_t sf_program(uint32_t address, const void *data, size_t length);
sf_status_t sf_verify(uint32_t address, const void *data, size_t length);
sf_status_t sf_last_status(void);

#endif
