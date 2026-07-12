/*
 * Power-loss-safe OTA boot-control record contract.
 *
 * Use ota_boot_control_storage_load() to select the newest committed copy and
 * ota_boot_control_storage_store() to atomically replace the older copy. The
 * application and bootloader share this binary on-flash format.
 */
#ifndef OTA_BOOT_CONTROL_H
#define OTA_BOOT_CONTROL_H

#include "ota_layout.h"
#include <stddef.h>
#include <stdint.h>

#define OTA_BOOT_CONTROL_MAGIC                 0x4F544332UL /* OTC2 */
#define OTA_BOOT_CONTROL_SCHEMA_VERSION        2UL
#define OTA_BOOT_CONTROL_COMMIT_ERASED         0xFFFFFFFFUL
#define OTA_BOOT_CONTROL_COMMIT_VALUE          0x434F4D54UL /* COMT */
#define OTA_BOOT_CONTROL_DEFAULT_TRIAL_LIMIT   3UL

typedef enum
{
    OTA_CONTROL_STATE_EMPTY = 0,
    OTA_CONTROL_STATE_CONFIRMED = 1,
    OTA_CONTROL_STATE_DOWNLOADING = 2,
    OTA_CONTROL_STATE_VERIFIED = 3,
    OTA_CONTROL_STATE_PENDING = 4,
    OTA_CONTROL_STATE_INSTALLING = 5,
    OTA_CONTROL_STATE_TRIAL = 6,
    OTA_CONTROL_STATE_ROLLBACK = 7,
    OTA_CONTROL_STATE_RECOVERY = 8
} ota_control_state_t;

typedef enum
{
    OTA_SLOT_STATE_EMPTY = 0,
    OTA_SLOT_STATE_DOWNLOADING = 1,
    OTA_SLOT_STATE_VERIFIED = 2,
    OTA_SLOT_STATE_CONFIRMED = 3,
    OTA_SLOT_STATE_REJECTED = 4
} ota_slot_state_t;

typedef enum
{
    OTA_CONTROL_ERROR_NONE = 0,
    OTA_CONTROL_ERROR_BAD_RECORD = 1,
    OTA_CONTROL_ERROR_EXTERNAL_FLASH = 2,
    OTA_CONTROL_ERROR_IMAGE_BOUNDS = 3,
    OTA_CONTROL_ERROR_IMAGE_CRC = 4,
    OTA_CONTROL_ERROR_IMAGE_SHA256 = 5,
    OTA_CONTROL_ERROR_IMAGE_SIGNATURE = 6,
    OTA_CONTROL_ERROR_INTERNAL_ERASE = 7,
    OTA_CONTROL_ERROR_INTERNAL_PROGRAM = 8,
    OTA_CONTROL_ERROR_INTERNAL_VERIFY = 9,
    OTA_CONTROL_ERROR_TRIAL_TIMEOUT = 10,
    OTA_CONTROL_ERROR_TRIAL_RESET = 11,
    OTA_CONTROL_ERROR_ROLLBACK_FAILED = 12,
    OTA_CONTROL_ERROR_INCOMPATIBLE_HARDWARE = 13,
    OTA_CONTROL_ERROR_VERSION_ROLLBACK = 14
} ota_control_error_t;

typedef enum
{
    OTA_CONTROL_COPY_NONE = 0,
    OTA_CONTROL_COPY_A = 1,
    OTA_CONTROL_COPY_B = 2
} ota_control_copy_t;

typedef enum
{
    OTA_CONTROL_STATUS_OK = 0,
    OTA_CONTROL_STATUS_INVALID_ARGUMENT = 1,
    OTA_CONTROL_STATUS_NO_VALID_RECORD = 2,
    OTA_CONTROL_STATUS_BAD_RECORD = 3,
    OTA_CONTROL_STATUS_IO_ERROR = 4,
    OTA_CONTROL_STATUS_VERIFY_FAILED = 5
} ota_control_status_t;

typedef struct
{
    uint32_t state;
    uint32_t image_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_flags;
    uint32_t load_address;
    uint32_t entry_address;
    uint8_t image_sha256[32];
    uint8_t signature[64];
} ota_firmware_descriptor_t;

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t record_size;
    uint32_t sequence;
    uint32_t state;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t trial_boot_count;
    uint32_t trial_boot_limit;
    uint32_t minimum_version;
    uint32_t boot_count;
    uint32_t last_reset_reason;
    uint32_t last_error;
    uint32_t last_error_address;
    uint32_t flags;
    ota_firmware_descriptor_t slots[2];
    uint32_t reserved[8];
    uint32_t record_crc32;
    uint32_t commit_marker;
} ota_boot_control_record_t;

typedef struct
{
    void *context;
    uint8_t (*read)(void *context, uint32_t address, uint8_t *data, uint32_t size);
    uint8_t (*erase_sector)(void *context, uint32_t address);
    uint8_t (*write)(void *context, uint32_t address, const uint8_t *data, uint32_t size);
} ota_boot_control_storage_t;

/* Initialize an uncommitted empty record suitable for first-time provisioning. */
void ota_boot_control_init(ota_boot_control_record_t *record);

/* Prepare CRC and erased commit marker before storage writes begin. */
void ota_boot_control_prepare(ota_boot_control_record_t *record);

/* Mark an already verified in-memory record as committed. */
void ota_boot_control_mark_committed(ota_boot_control_record_t *record);

/* Validate record fields and CRC; body validation permits an erased commit word. */
uint8_t ota_boot_control_body_is_valid(const ota_boot_control_record_t *record);
uint8_t ota_boot_control_is_valid(const ota_boot_control_record_t *record);

/* Select the newest valid committed copy using wrap-safe sequence comparison. */
ota_control_status_t ota_boot_control_select(
    const ota_boot_control_record_t *copy_a,
    const ota_boot_control_record_t *copy_b,
    ota_boot_control_record_t *selected,
    ota_control_copy_t *selected_copy);

/* Return nonzero only for a legal persistent state-machine transition. */
uint8_t ota_boot_control_transition_is_allowed(uint32_t from_state, uint32_t to_state);

/* Read both sectors and return the newest committed record. */
ota_control_status_t ota_boot_control_storage_load(
    const ota_boot_control_storage_t *storage,
    ota_boot_control_record_t *record,
    ota_control_copy_t *source_copy);

/* Write, verify and commit a new record into the non-current metadata sector. */
ota_control_status_t ota_boot_control_storage_store(
    const ota_boot_control_storage_t *storage,
    const ota_boot_control_record_t *requested,
    ota_boot_control_record_t *committed,
    ota_control_copy_t *committed_copy);

#endif /* OTA_BOOT_CONTROL_H */
