#ifndef APP_FLASH_CHECK_H
#define APP_FLASH_CHECK_H

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifndef APP_FLASH_DESTRUCTIVE_TEST_ENABLE
#define APP_FLASH_DESTRUCTIVE_TEST_ENABLE 0U
#endif

typedef enum
{
    APP_FLASH_TEST_STAGE_IDLE = 0,
    APP_FLASH_TEST_STAGE_PRECHECK,
    APP_FLASH_TEST_STAGE_ERASE,
    APP_FLASH_TEST_STAGE_ERASE_VERIFY,
    APP_FLASH_TEST_STAGE_WRITE,
    APP_FLASH_TEST_STAGE_READBACK,
    APP_FLASH_TEST_STAGE_COMPARE,
    APP_FLASH_TEST_STAGE_CLEANUP,
    APP_FLASH_TEST_STAGE_COMPLETE
} app_flash_test_stage_t;

typedef struct
{
    app_flash_test_stage_t stage;
    HAL_StatusTypeDef cleanup_status;
    uint32_t expected_crc32;
    uint32_t actual_crc32;
    uint32_t first_error_offset;
    uint8_t restored_erased;
} app_flash_test_result_t;

HAL_StatusTypeDef app_flash_crc32(uint32_t address,
                                  size_t length,
                                  uint32_t *crc32);
HAL_StatusTypeDef app_flash_run_safe_test(app_flash_test_result_t *result);
const char *app_flash_test_stage_name(app_flash_test_stage_t stage);

#endif
