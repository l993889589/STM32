#ifndef GATEWAY_OTA_FORMAT_H
#define GATEWAY_OTA_FORMAT_H

#include <stdint.h>

#define GATEWAY_OTA_MANIFEST_MAGIC          0x4F54414DUL
#define GATEWAY_OTA_MANIFEST_SCHEMA         1UL
#define GATEWAY_OTA_MANIFEST_SIZE           188UL

#define GATEWAY_OTA_IMAGE_FLAG_SIGNED       0x00000002UL
#define GATEWAY_OTA_LOAD_ADDRESS            0x90000000UL
#define GATEWAY_OTA_EXEC_SIZE               0x00200000UL

#define GATEWAY_OTA_STAGE_ADDRESS           0x00400000UL
#define GATEWAY_OTA_STAGE_SIZE              0x00200000UL
#define GATEWAY_OTA_STAGE_IMAGE_ADDRESS     0x00401000UL
#define GATEWAY_OTA_STAGE_IMAGE_CAPACITY    0x001FF000UL
#define GATEWAY_OTA_STAGE_COMPLETE_ADDRESS  0x00400100UL
#define GATEWAY_OTA_STAGE_REQUEST_ADDRESS   0x00400104UL
#define GATEWAY_OTA_STAGE_CONSUMED_ADDRESS  0x00400108UL
#define GATEWAY_OTA_MARKER_SET              0x00000000UL
#define GATEWAY_OTA_MARKER_ERASED           0xFFFFFFFFUL

#define GATEWAY_OTA_HEALTH_MAGIC            0x4B4F3748UL
#define GATEWAY_OTA_TRIAL_MAGIC             0x4C495237UL

typedef enum
{
    GATEWAY_OTA_BOOT_ERROR_NONE = 0,
    GATEWAY_OTA_BOOT_ERROR_STAGE_IO = 1,
    GATEWAY_OTA_BOOT_ERROR_MANIFEST = 2,
    GATEWAY_OTA_BOOT_ERROR_VERSION = 3,
    GATEWAY_OTA_BOOT_ERROR_IMAGE_CRC = 4,
    GATEWAY_OTA_BOOT_ERROR_IMAGE_SHA256 = 5,
    GATEWAY_OTA_BOOT_ERROR_SIGNATURE = 6,
    GATEWAY_OTA_BOOT_ERROR_SLOT_WRITE = 7,
    GATEWAY_OTA_BOOT_ERROR_EXEC_WRITE = 8,
    GATEWAY_OTA_BOOT_ERROR_CONTROL = 9,
    GATEWAY_OTA_BOOT_ERROR_TRIAL_ROLLBACK = 10
} gateway_ota_boot_error_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t boot_state;
    uint32_t image_size;
    uint32_t image_version;
    uint32_t image_flags;
    uint32_t image_crc32;
    uint32_t package_address;
    uint32_t package_size;
    uint32_t rollback_address;
    uint32_t rollback_size;
    uint32_t rollback_crc32;
    uint32_t load_address;
    uint32_t entry_address;
    uint8_t image_sha256[32];
    uint8_t package_sha256[32];
    uint8_t signature[64];
    uint32_t manifest_crc32;
} gateway_ota_manifest_t;

typedef char gateway_ota_manifest_size_must_be_188[
    (sizeof(gateway_ota_manifest_t) == GATEWAY_OTA_MANIFEST_SIZE) ? 1 : -1];

#endif
