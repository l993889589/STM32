#include "drv_w25qxx.h"
#include "mock_flash.h"

#include <string.h>

#define MOCK_FLASH_SIZE 0x3000U
#define MOCK_SECTOR_SIZE 0x1000U

static uint8_t s_flash[MOCK_FLASH_SIZE];
static uint32_t s_program_calls;
static uint32_t s_fail_program_call;

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

void mock_flash_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_program_calls = 0U;
    s_fail_program_call = 0U;
}

void mock_flash_fail_program_call(uint32_t call_index)
{
    s_fail_program_call = call_index;
}

void mock_flash_corrupt(uint32_t address, uint8_t value)
{
    if(address < sizeof(s_flash))
        s_flash[address] = value;
}

void mock_flash_write_legacy(uint8_t address, uint8_t mode,
                             uint16_t manual_pwm, uint16_t auto_pwm,
                             uint32_t restart_count)
{
    uint8_t legacy[16];

    memset(legacy, 0, sizeof(legacy));
    write_le32(&legacy[0], 0x00000101U);
    legacy[4] = 1U;
    legacy[5] = address;
    legacy[6] = mode;
    write_le16(&legacy[8], manual_pwm);
    write_le16(&legacy[10], auto_pwm);
    write_le32(&legacy[12], restart_count);
    memcpy(s_flash, legacy, sizeof(legacy));
}

uint32_t mock_flash_program_calls(void)
{
    return s_program_calls;
}

sf_status_t sf_last_status(void)
{
    return SF_STATUS_OK;
}

sf_status_t sf_read(uint32_t address, void *data, size_t length)
{
    if(data == NULL || address > sizeof(s_flash) ||
       length > sizeof(s_flash) - address)
        return SF_STATUS_INVALID_ARGUMENT;
    memcpy(data, &s_flash[address], length);
    return SF_STATUS_OK;
}

sf_status_t sf_erase_sector_checked(uint32_t address)
{
    address &= ~(MOCK_SECTOR_SIZE - 1U);
    if(address > sizeof(s_flash) - MOCK_SECTOR_SIZE)
        return SF_STATUS_INVALID_ARGUMENT;
    memset(&s_flash[address], 0xFF, MOCK_SECTOR_SIZE);
    return SF_STATUS_OK;
}

sf_status_t sf_program(uint32_t address, const void *data, size_t length)
{
    const uint8_t *input = (const uint8_t *)data;

    s_program_calls++;
    if(s_fail_program_call != 0U && s_program_calls == s_fail_program_call)
        return SF_STATUS_IO_ERROR;
    if(data == NULL || address > sizeof(s_flash) ||
       length > sizeof(s_flash) - address)
        return SF_STATUS_INVALID_ARGUMENT;
    for(size_t index = 0U; index < length; index++)
    {
        if((uint8_t)(~s_flash[address + index]) & input[index])
            return SF_STATUS_IO_ERROR;
        s_flash[address + index] &= input[index];
    }
    return SF_STATUS_OK;
}

sf_status_t sf_verify(uint32_t address, const void *data, size_t length)
{
    if(data == NULL || address > sizeof(s_flash) ||
       length > sizeof(s_flash) - address)
        return SF_STATUS_INVALID_ARGUMENT;
    return memcmp(&s_flash[address], data, length) == 0 ?
           SF_STATUS_OK : SF_STATUS_VERIFY_FAILED;
}
