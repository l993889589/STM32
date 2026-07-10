#ifndef OTA_BOOT_H
#define OTA_BOOT_H

#include "ota_layout.h"
#include <stdint.h>

typedef enum
{
    OTA_BOOT_RESULT_NO_UPDATE = 0,
    OTA_BOOT_RESULT_INSTALLED = 1,
    OTA_BOOT_RESULT_BAD_MANIFEST = 2,
    OTA_BOOT_RESULT_BAD_IMAGE = 3,
    OTA_BOOT_RESULT_FLASH_ERROR = 4,
    OTA_BOOT_RESULT_UNSUPPORTED = 5,
    OTA_BOOT_RESULT_ROLLED_BACK = 6,
    OTA_BOOT_RESULT_ROLLBACK_FAILED = 7,
    OTA_BOOT_RESULT_RECOVERY_REQUIRED = 8
} ota_boot_result_t;

ota_boot_result_t ota_boot_process_update(void);
uint8_t ota_boot_app_is_valid(void);

#endif /* OTA_BOOT_H */
