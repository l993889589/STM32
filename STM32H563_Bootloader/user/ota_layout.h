#ifndef OTA_LAYOUT_H
#define OTA_LAYOUT_H

#include <stdint.h>

#define OTA_INTERNAL_FLASH_BASE         0x08000000UL
#define OTA_INTERNAL_FLASH_SIZE         0x00200000UL
#define OTA_SRAM_BASE                   0x20000000UL
#define OTA_SRAM_SIZE                   0x000A0000UL

#define OTA_BOOTLOADER_BASE             0x08000000UL
#define OTA_BOOTLOADER_SIZE             0x00020000UL
#define OTA_APP_BASE                    (OTA_BOOTLOADER_BASE + OTA_BOOTLOADER_SIZE)
#define OTA_APP_SIZE                    (OTA_INTERNAL_FLASH_SIZE - OTA_BOOTLOADER_SIZE)

#define OTA_EXT_FLASH_SIZE              (16UL * 1024UL * 1024UL)
#define OTA_EXT_SECTOR_SIZE             4096UL

#define OTA_EXT_MANIFEST_A_ADDR         0x00000000UL
#define OTA_EXT_MANIFEST_B_ADDR         0x00001000UL
#define OTA_EXT_STATUS_LOG_ADDR         0x00002000UL
#define OTA_EXT_DOWNLOAD_ADDR           0x00100000UL
#define OTA_EXT_DOWNLOAD_SIZE           0x00600000UL
#define OTA_EXT_BACKUP_ADDR             0x00800000UL
#define OTA_EXT_BACKUP_SIZE             0x00600000UL

#define OTA_MANIFEST_MAGIC              0x4F54414DUL /* OTAM */
#define OTA_MANIFEST_VERSION            1UL

#define OTA_IMAGE_FLAG_ENCRYPTED        0x00000001UL
#define OTA_IMAGE_FLAG_SIGNED           0x00000002UL

typedef enum
{
    OTA_BOOT_STATE_NORMAL = 0,
    OTA_BOOT_STATE_PENDING_UPDATE = 1,
    OTA_BOOT_STATE_INSTALLING = 2,
    OTA_BOOT_STATE_TRIAL_BOOT = 3,
    OTA_BOOT_STATE_CONFIRMED = 4,
    OTA_BOOT_STATE_ROLLBACK_REQUIRED = 5,
    OTA_BOOT_STATE_ROLLING_BACK = 6
} ota_boot_state_t;

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
} ota_manifest_t;

#endif /* OTA_LAYOUT_H */
