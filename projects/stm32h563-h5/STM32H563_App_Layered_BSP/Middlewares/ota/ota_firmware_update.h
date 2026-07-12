/*
 * Transport-independent external firmware A/B update transaction.
 *
 * USB and HTTP integrations call this API with relative image offsets. The
 * module selects and owns the inactive slot, verifies every write, validates
 * the complete CRC32, and publishes power-safe VERIFIED/PENDING records.
 */
#ifndef OTA_FIRMWARE_UPDATE_H
#define OTA_FIRMWARE_UPDATE_H

#include "ota_boot_control.h"
#include <stdint.h>

#define OTA_FIRMWARE_UPDATE_VERIFY_BUFFER_SIZE  256U

typedef enum
{
    OTA_FIRMWARE_UPDATE_OK = 0,
    OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT = 1,
    OTA_FIRMWARE_UPDATE_NOT_PROVISIONED = 2,
    OTA_FIRMWARE_UPDATE_BUSY = 3,
    OTA_FIRMWARE_UPDATE_BAD_STATE = 4,
    OTA_FIRMWARE_UPDATE_BAD_RANGE = 5,
    OTA_FIRMWARE_UPDATE_SEQUENCE = 6,
    OTA_FIRMWARE_UPDATE_IO_ERROR = 7,
    OTA_FIRMWARE_UPDATE_VERIFY_FAILED = 8,
    OTA_FIRMWARE_UPDATE_CRC_MISMATCH = 9,
    OTA_FIRMWARE_UPDATE_CONTROL_ERROR = 10,
    OTA_FIRMWARE_UPDATE_SHA256_MISMATCH = 11,
    OTA_FIRMWARE_UPDATE_VERSION_ROLLBACK = 12
} ota_firmware_update_status_t;

typedef struct
{
    void *context;
    uint8_t (*read)(void *context, uint32_t address, uint8_t *data, uint32_t size);
    uint8_t (*erase_sector)(void *context, uint32_t address);
    uint8_t (*write)(void *context, uint32_t address, const uint8_t *data, uint32_t size);
} ota_firmware_update_storage_t;

typedef struct
{
    ota_firmware_update_storage_t storage;
    ota_boot_control_record_t control;
    uint32_t target_address;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t expected_crc32;
    uint32_t target_slot;
    uint32_t abort_state;
    uint8_t verify_buffer[OTA_FIRMWARE_UPDATE_VERIFY_BUFFER_SIZE];
    uint8_t is_active;
} ota_firmware_update_t;

/* Reset a transaction context and bind its external-flash operations. */
ota_firmware_update_status_t ota_firmware_update_init(
    ota_firmware_update_t *update,
    const ota_firmware_update_storage_t *storage);

/* Select, record and erase the inactive firmware slot for a new package. */
ota_firmware_update_status_t ota_firmware_update_begin(
    ota_firmware_update_t *update,
    const ota_firmware_descriptor_t *descriptor);

/* Begin an update from Boot recovery, including an unprovisioned device. */
ota_firmware_update_status_t ota_firmware_update_begin_recovery(
    ota_firmware_update_t *update,
    const ota_firmware_descriptor_t *descriptor);

/* Write the next sequential package block at a slot-relative offset. */
ota_firmware_update_status_t ota_firmware_update_write(
    ota_firmware_update_t *update,
    uint32_t offset,
    const uint8_t *data,
    uint32_t size);

/* Verify the full slot and atomically request bootloader installation. */
ota_firmware_update_status_t ota_firmware_update_finish(ota_firmware_update_t *update);

/* Cancel a partial transaction without erasing or changing the active slot. */
ota_firmware_update_status_t ota_firmware_update_abort(ota_firmware_update_t *update);

#endif /* OTA_FIRMWARE_UPDATE_H */
