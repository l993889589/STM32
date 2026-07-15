#include <string.h>
#include "app_flash_check.h"
#include "bsp.h"

#define APP_FLASH_CRC32_POLYNOMIAL        0xEDB88320UL
#define APP_FLASH_TEST_PATTERN_OFFSET     123UL
#define APP_FLASH_TEST_PATTERN_LENGTH     513UL

static uint8_t app_flash_buffer[BSP_W25Q128_SECTOR_SIZE];

static uint32_t app_flash_crc32_update(uint32_t crc32,
                                       const uint8_t *data,
                                       size_t length);
static uint32_t app_flash_buffer_crc32(const uint8_t *data, size_t length);
static uint8_t app_flash_pattern_byte(uint32_t index);
static int app_flash_buffer_is_erased(const uint8_t *data,
                                      size_t length,
                                      uint32_t *first_error_offset);
static HAL_StatusTypeDef app_flash_cleanup_test_sector(
    app_flash_test_result_t *result);

HAL_StatusTypeDef app_flash_crc32(uint32_t address,
                                  size_t length,
                                  uint32_t *crc32)
{
    size_t offset = 0U;
    uint32_t value = 0xFFFFFFFFUL;

    if (crc32 == NULL)
    {
        return HAL_ERROR;
    }

    if ((address >= BSP_W25Q128_TOTAL_SIZE) ||
        (length > (size_t)(BSP_W25Q128_TOTAL_SIZE - address)))
    {
        return HAL_ERROR;
    }

    while (offset < length)
    {
        size_t remaining = length - offset;
        size_t chunk_length = (remaining > sizeof(app_flash_buffer)) ?
                              sizeof(app_flash_buffer) :
                              remaining;
        HAL_StatusTypeDef status = bsp_w25q128_read(address + (uint32_t)offset,
                                                     app_flash_buffer,
                                                     chunk_length);

        if (status != HAL_OK)
        {
            return status;
        }

        value = app_flash_crc32_update(value,
                                       app_flash_buffer,
                                       chunk_length);
        offset += chunk_length;
    }

    *crc32 = value ^ 0xFFFFFFFFUL;
    return HAL_OK;
}

HAL_StatusTypeDef app_flash_run_safe_test(app_flash_test_result_t *result)
{
    HAL_StatusTypeDef status;
    uint32_t index;

    if (result == NULL)
    {
        return HAL_ERROR;
    }

    memset(result, 0, sizeof(*result));
    result->stage = APP_FLASH_TEST_STAGE_PRECHECK;
    result->cleanup_status = HAL_OK;
    result->first_error_offset = UINT32_MAX;

    status = bsp_w25q128_read(BSP_FLASH_DIAGNOSTIC_ADDRESS,
                              app_flash_buffer,
                              BSP_FLASH_DIAGNOSTIC_SIZE);
    if (status != HAL_OK)
    {
        return status;
    }

    if (app_flash_buffer_is_erased(app_flash_buffer,
                                   BSP_FLASH_DIAGNOSTIC_SIZE,
                                   &result->first_error_offset) == 0)
    {
        return HAL_BUSY;
    }

    result->stage = APP_FLASH_TEST_STAGE_ERASE;
    status = bsp_w25q128_erase_sector(BSP_FLASH_DIAGNOSTIC_ADDRESS);
    if (status != HAL_OK)
    {
        return status;
    }

    result->stage = APP_FLASH_TEST_STAGE_ERASE_VERIFY;
    status = bsp_w25q128_read(BSP_FLASH_DIAGNOSTIC_ADDRESS,
                              app_flash_buffer,
                              BSP_FLASH_DIAGNOSTIC_SIZE);
    if (status != HAL_OK)
    {
        return status;
    }

    if (app_flash_buffer_is_erased(app_flash_buffer,
                                   BSP_FLASH_DIAGNOSTIC_SIZE,
                                   &result->first_error_offset) == 0)
    {
        return HAL_ERROR;
    }

    for (index = 0U; index < APP_FLASH_TEST_PATTERN_LENGTH; index++)
    {
        app_flash_buffer[index] = app_flash_pattern_byte(index);
    }
    result->expected_crc32 = app_flash_buffer_crc32(app_flash_buffer,
                                                    APP_FLASH_TEST_PATTERN_LENGTH);

    result->stage = APP_FLASH_TEST_STAGE_WRITE;
    status = bsp_w25q128_write(BSP_FLASH_DIAGNOSTIC_ADDRESS +
                                   APP_FLASH_TEST_PATTERN_OFFSET,
                               app_flash_buffer,
                               APP_FLASH_TEST_PATTERN_LENGTH);
    if (status != HAL_OK)
    {
        result->cleanup_status = app_flash_cleanup_test_sector(result);
        return status;
    }

    memset(app_flash_buffer, 0, APP_FLASH_TEST_PATTERN_LENGTH);
    result->stage = APP_FLASH_TEST_STAGE_READBACK;
    status = bsp_w25q128_read(BSP_FLASH_DIAGNOSTIC_ADDRESS +
                                  APP_FLASH_TEST_PATTERN_OFFSET,
                              app_flash_buffer,
                              APP_FLASH_TEST_PATTERN_LENGTH);
    if (status != HAL_OK)
    {
        result->cleanup_status = app_flash_cleanup_test_sector(result);
        return status;
    }

    result->actual_crc32 = app_flash_buffer_crc32(app_flash_buffer,
                                                  APP_FLASH_TEST_PATTERN_LENGTH);
    result->stage = APP_FLASH_TEST_STAGE_COMPARE;
    for (index = 0U; index < APP_FLASH_TEST_PATTERN_LENGTH; index++)
    {
        if (app_flash_buffer[index] != app_flash_pattern_byte(index))
        {
            result->first_error_offset = APP_FLASH_TEST_PATTERN_OFFSET + index;
            result->cleanup_status = app_flash_cleanup_test_sector(result);
            return HAL_ERROR;
        }
    }

    status = app_flash_cleanup_test_sector(result);
    result->cleanup_status = status;
    if (status != HAL_OK)
    {
        result->stage = APP_FLASH_TEST_STAGE_CLEANUP;
        return status;
    }

    result->stage = APP_FLASH_TEST_STAGE_COMPLETE;
    return HAL_OK;
}

const char *app_flash_test_stage_name(app_flash_test_stage_t stage)
{
    switch (stage)
    {
        case APP_FLASH_TEST_STAGE_IDLE:
            return "idle";
        case APP_FLASH_TEST_STAGE_PRECHECK:
            return "precheck";
        case APP_FLASH_TEST_STAGE_ERASE:
            return "erase";
        case APP_FLASH_TEST_STAGE_ERASE_VERIFY:
            return "erase_verify";
        case APP_FLASH_TEST_STAGE_WRITE:
            return "write";
        case APP_FLASH_TEST_STAGE_READBACK:
            return "readback";
        case APP_FLASH_TEST_STAGE_COMPARE:
            return "compare";
        case APP_FLASH_TEST_STAGE_CLEANUP:
            return "cleanup";
        case APP_FLASH_TEST_STAGE_COMPLETE:
            return "complete";
        default:
            return "unknown";
    }
}

static uint32_t app_flash_crc32_update(uint32_t crc32,
                                       const uint8_t *data,
                                       size_t length)
{
    size_t index;

    for (index = 0U; index < length; index++)
    {
        uint32_t bit;

        crc32 ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc32 = ((crc32 & 1UL) != 0UL) ?
                    ((crc32 >> 1) ^ APP_FLASH_CRC32_POLYNOMIAL) :
                    (crc32 >> 1);
        }
    }

    return crc32;
}

static uint32_t app_flash_buffer_crc32(const uint8_t *data, size_t length)
{
    return app_flash_crc32_update(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
}

static uint8_t app_flash_pattern_byte(uint32_t index)
{
    return (uint8_t)(((index * 37UL) + 0x5AUL) ^ (index >> 1));
}

static int app_flash_buffer_is_erased(const uint8_t *data,
                                      size_t length,
                                      uint32_t *first_error_offset)
{
    size_t index;

    for (index = 0U; index < length; index++)
    {
        if (data[index] != 0xFFU)
        {
            if (first_error_offset != NULL)
            {
                *first_error_offset = (uint32_t)index;
            }
            return 0;
        }
    }

    return 1;
}

static HAL_StatusTypeDef app_flash_cleanup_test_sector(
    app_flash_test_result_t *result)
{
    HAL_StatusTypeDef status = bsp_w25q128_erase_sector(
        BSP_FLASH_DIAGNOSTIC_ADDRESS);

    if (status != HAL_OK)
    {
        return status;
    }

    status = bsp_w25q128_read(BSP_FLASH_DIAGNOSTIC_ADDRESS,
                              app_flash_buffer,
                              BSP_FLASH_DIAGNOSTIC_SIZE);
    if (status != HAL_OK)
    {
        return status;
    }

    if (app_flash_buffer_is_erased(app_flash_buffer,
                                   BSP_FLASH_DIAGNOSTIC_SIZE,
                                   &result->first_error_offset) == 0)
    {
        return HAL_ERROR;
    }

    result->restored_erased = 1U;
    return HAL_OK;
}
