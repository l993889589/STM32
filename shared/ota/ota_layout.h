/*
 * Shared STM32H563 OTA memory layout and version-1 manifest contract.
 *
 * Both the bootloader and application include this file through their local
 * compatibility headers. Keep all persistent addresses and on-flash types in
 * this file so the two images cannot silently drift to different layouts.
 */
#ifndef SHARED_OTA_LAYOUT_H
#define SHARED_OTA_LAYOUT_H

#include <stdint.h>

#define OTA_INTERNAL_FLASH_BASE          0x08000000UL
#define OTA_INTERNAL_FLASH_SIZE          0x00200000UL
#define OTA_SRAM_BASE                    0x20000000UL
#define OTA_SRAM_SIZE                    0x000A0000UL

#define OTA_BOOTLOADER_BASE              0x08000000UL
#define OTA_BOOTLOADER_SIZE              0x00020000UL
#define OTA_APP_BASE                     (OTA_BOOTLOADER_BASE + OTA_BOOTLOADER_SIZE)
#define OTA_APP_SIZE                     (OTA_INTERNAL_FLASH_SIZE - OTA_BOOTLOADER_SIZE)

#define OTA_EXT_FLASH_SIZE               0x01000000UL
#define OTA_EXT_SECTOR_SIZE              0x00001000UL

/* Redundant boot-control records and their append-only diagnostic area. */
#define OTA_EXT_BOOT_CONTROL_ADDR         0x00000000UL
#define OTA_EXT_BOOT_CONTROL_SIZE         0x00010000UL
#define OTA_EXT_MANIFEST_A_ADDR           0x00000000UL
#define OTA_EXT_MANIFEST_B_ADDR           0x00001000UL
#define OTA_EXT_STATUS_LOG_ADDR           0x00002000UL
#define OTA_EXT_STATUS_LOG_SIZE           0x0000E000UL

/* Persistent firmware package slots. Only the inactive slot may be erased. */
#define OTA_EXT_FIRMWARE_SLOT_A_ADDR       0x00010000UL
#define OTA_EXT_FIRMWARE_SLOT_B_ADDR       0x00210000UL
#define OTA_EXT_FIRMWARE_SLOT_SIZE         0x00200000UL

/* Crash records, device configuration and future boot diagnostics. */
#define OTA_EXT_DIAGNOSTIC_ADDR            0x00410000UL
#define OTA_EXT_DIAGNOSTIC_SIZE            0x000F0000UL

#define UI_ASSET_SLOT_A_ADDR               0x00500000UL
#define UI_ASSET_SLOT_B_ADDR               0x00A00000UL
#define UI_ASSET_SLOT_SIZE                 0x00500000UL

#define OTA_EXT_FACTORY_RESERVED_ADDR      0x00F00000UL
#define OTA_EXT_FACTORY_RESERVED_SIZE      0x00100000UL

/* Version-1 compatibility aliases. They are removed after the A/B migration. */
#define OTA_EXT_STATUS_AREA_SIZE           OTA_EXT_BOOT_CONTROL_SIZE
#define OTA_EXT_DOWNLOAD_ADDR              OTA_EXT_FIRMWARE_SLOT_A_ADDR
#define OTA_EXT_DOWNLOAD_SIZE              OTA_EXT_FIRMWARE_SLOT_SIZE
#define OTA_EXT_BACKUP_ADDR                OTA_EXT_FIRMWARE_SLOT_B_ADDR
#define OTA_EXT_BACKUP_SIZE                OTA_EXT_FIRMWARE_SLOT_SIZE
#define UI_ASSET_RESERVED_ADDR             OTA_EXT_FACTORY_RESERVED_ADDR
#define UI_ASSET_RESERVED_SIZE             OTA_EXT_FACTORY_RESERVED_SIZE

#define OTA_MANIFEST_MAGIC                 0x4F54414DUL /* OTAM */
#define OTA_MANIFEST_VERSION               1UL

#define OTA_IMAGE_FLAG_ENCRYPTED           0x00000001UL
#define OTA_IMAGE_FLAG_SIGNED              0x00000002UL

typedef enum
{
    OTA_FIRMWARE_SLOT_A = 0,
    OTA_FIRMWARE_SLOT_B = 1,
    OTA_FIRMWARE_SLOT_NONE = 0xFF
} ota_firmware_slot_t;

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

/* Compile-time guards for the persistent memory contract. */
#if OTA_APP_BASE != 0x08020000UL
#error "Unexpected STM32H563 application base"
#endif

#if (OTA_EXT_MANIFEST_A_ADDR % OTA_EXT_SECTOR_SIZE) != 0UL || \
    (OTA_EXT_MANIFEST_B_ADDR % OTA_EXT_SECTOR_SIZE) != 0UL || \
    (OTA_EXT_FIRMWARE_SLOT_A_ADDR % OTA_EXT_SECTOR_SIZE) != 0UL || \
    (OTA_EXT_FIRMWARE_SLOT_B_ADDR % OTA_EXT_SECTOR_SIZE) != 0UL
#error "OTA persistent regions must be sector aligned"
#endif

#if (OTA_EXT_BOOT_CONTROL_ADDR + OTA_EXT_BOOT_CONTROL_SIZE) != OTA_EXT_FIRMWARE_SLOT_A_ADDR
#error "Boot-control and firmware slot A regions are not contiguous"
#endif

#if (OTA_EXT_FIRMWARE_SLOT_A_ADDR + OTA_EXT_FIRMWARE_SLOT_SIZE) != OTA_EXT_FIRMWARE_SLOT_B_ADDR
#error "Firmware slot A and slot B regions overlap or have a gap"
#endif

#if (OTA_EXT_FIRMWARE_SLOT_B_ADDR + OTA_EXT_FIRMWARE_SLOT_SIZE) != OTA_EXT_DIAGNOSTIC_ADDR
#error "Firmware slot B and diagnostic regions overlap or have a gap"
#endif

#if (OTA_EXT_DIAGNOSTIC_ADDR + OTA_EXT_DIAGNOSTIC_SIZE) != UI_ASSET_SLOT_A_ADDR
#error "Diagnostic and UI slot A regions overlap or have a gap"
#endif

#if (UI_ASSET_SLOT_A_ADDR + UI_ASSET_SLOT_SIZE) != UI_ASSET_SLOT_B_ADDR
#error "UI asset slots overlap or have a gap"
#endif

#if (UI_ASSET_SLOT_B_ADDR + UI_ASSET_SLOT_SIZE) != OTA_EXT_FACTORY_RESERVED_ADDR
#error "UI slot B and factory-reserved regions overlap or have a gap"
#endif

#if (OTA_EXT_FACTORY_RESERVED_ADDR + OTA_EXT_FACTORY_RESERVED_SIZE) != OTA_EXT_FLASH_SIZE
#error "External flash layout does not cover exactly 16 MiB"
#endif

#endif /* SHARED_OTA_LAYOUT_H */
