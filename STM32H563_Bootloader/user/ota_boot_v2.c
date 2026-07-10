/*
 * STM32H563 bootloader runtime for persistent external firmware A/B records.
 *
 * A confirmed package remains immutable in the active external slot while a
 * candidate occupies the pending slot. Internal App flash is only an execution
 * copy and can always be reconstructed from one of those verified packages.
 */
#include "ota_boot_v2.h"
#include "ota_boot_private.h"
#include "boot_security.h"
#include "../../shared/ota/ota_boot_control.h"
#include "gd25lq128.h"
#include <string.h>

static uint8_t ota_boot_v2_read(
    void *context,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    (void)context;
    return gd25lq128_read(address, data, size);
}

static uint8_t ota_boot_v2_erase(void *context, uint32_t address)
{
    (void)context;
    return gd25lq128_erase_4k(address);
}

static uint8_t ota_boot_v2_write(
    void *context,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    (void)context;
    return gd25lq128_write(address, data, size);
}

static ota_boot_control_storage_t ota_boot_v2_storage(void)
{
    ota_boot_control_storage_t storage;

    storage.context = NULL;
    storage.read = ota_boot_v2_read;
    storage.erase_sector = ota_boot_v2_erase;
    storage.write = ota_boot_v2_write;
    return storage;
}

static uint32_t ota_boot_v2_slot_address(uint32_t slot)
{
    return (slot == (uint32_t)OTA_FIRMWARE_SLOT_A) ?
           OTA_EXT_FIRMWARE_SLOT_A_ADDR : OTA_EXT_FIRMWARE_SLOT_B_ADDR;
}

static uint8_t ota_boot_v2_address_to_slot(uint32_t address, uint32_t *slot)
{
    if(address == OTA_EXT_FIRMWARE_SLOT_A_ADDR)
    {
        *slot = (uint32_t)OTA_FIRMWARE_SLOT_A;
        return 1U;
    }
    if(address == OTA_EXT_FIRMWARE_SLOT_B_ADDR)
    {
        *slot = (uint32_t)OTA_FIRMWARE_SLOT_B;
        return 1U;
    }
    return 0U;
}

static uint8_t ota_boot_v2_external_vector(
    uint32_t address,
    uint32_t image_size,
    uint32_t *entry_address)
{
    uint32_t vectors[2];

    if(image_size < sizeof(vectors) || entry_address == NULL ||
       !gd25lq128_read(address, (uint8_t *)vectors, sizeof(vectors)))
    {
        return 0U;
    }

    if(vectors[0] < OTA_SRAM_BASE || vectors[0] >= (OTA_SRAM_BASE + OTA_SRAM_SIZE) ||
       vectors[1] < OTA_APP_BASE || vectors[1] >= (OTA_APP_BASE + OTA_APP_SIZE) ||
       (vectors[1] & 1U) == 0U)
    {
        return 0U;
    }

    *entry_address = vectors[1];
    return 1U;
}

static void ota_boot_v2_descriptor_from_v1(
    ota_firmware_descriptor_t *descriptor,
    const ota_manifest_t *manifest,
    uint32_t state)
{
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->state = state;
    descriptor->image_version = manifest->image_version;
    descriptor->image_size = manifest->image_size;
    descriptor->image_crc32 = manifest->image_crc32;
    descriptor->image_flags = manifest->image_flags;
    descriptor->load_address = manifest->load_address;
    descriptor->entry_address = manifest->entry_address;
    memcpy(descriptor->image_sha256, manifest->image_sha256,
           sizeof(descriptor->image_sha256));
    memcpy(descriptor->signature, manifest->signature,
           sizeof(descriptor->signature));
}

static uint8_t ota_boot_v2_store(ota_boot_control_record_t *record)
{
    ota_boot_control_storage_t storage = ota_boot_v2_storage();
    ota_boot_control_record_t committed;
    ota_control_copy_t copy;

    if(ota_boot_control_storage_store(&storage, record, &committed, &copy) !=
       OTA_CONTROL_STATUS_OK)
    {
        return 0U;
    }

    *record = committed;
    return 1U;
}

static uint8_t ota_boot_v2_load(ota_boot_control_record_t *record)
{
    ota_boot_control_storage_t storage = ota_boot_v2_storage();
    ota_control_copy_t copy;

    return (ota_boot_control_storage_load(&storage, record, &copy) ==
            OTA_CONTROL_STATUS_OK) ? 1U : 0U;
}

static uint8_t ota_boot_v2_slot_descriptor_is_installable(
    const ota_boot_control_record_t *record,
    uint32_t slot)
{
    const ota_firmware_descriptor_t *descriptor;

    if(slot != (uint32_t)OTA_FIRMWARE_SLOT_A &&
       slot != (uint32_t)OTA_FIRMWARE_SLOT_B)
    {
        return 0U;
    }

    descriptor = &record->slots[slot];
    return (descriptor->state == (uint32_t)OTA_SLOT_STATE_VERIFIED ||
            descriptor->state == (uint32_t)OTA_SLOT_STATE_CONFIRMED) ? 1U : 0U;
}

static uint8_t ota_boot_v2_internal_matches_slot(
    const ota_boot_control_record_t *record,
    uint32_t slot)
{
    const ota_firmware_descriptor_t *descriptor = &record->slots[slot];

    return ota_boot_internal_image_matches(
        descriptor->image_size, descriptor->image_crc32);
}

static uint8_t ota_boot_v2_install_slot(
    ota_boot_control_record_t *record,
    uint32_t slot)
{
    const ota_firmware_descriptor_t *descriptor;
    uint32_t security_error;

    if(!ota_boot_v2_slot_descriptor_is_installable(record, slot))
    {
        return 0U;
    }

    descriptor = &record->slots[slot];
    if(!boot_security_verify_slot(record, slot, &security_error))
    {
        record->last_error = security_error;
        record->last_error_address = ota_boot_v2_slot_address(slot);
        return 0U;
    }
    return ota_boot_install_external_image(
        ota_boot_v2_slot_address(slot),
        descriptor->image_size,
        descriptor->image_crc32);
}

static ota_boot_result_t ota_boot_v2_enter_recovery(
    ota_boot_control_record_t *record,
    uint32_t error)
{
    record->state = (uint32_t)OTA_CONTROL_STATE_RECOVERY;
    record->last_error = error;
    (void)ota_boot_v2_store(record);
    return OTA_BOOT_RESULT_RECOVERY_REQUIRED;
}

static ota_boot_result_t ota_boot_v2_rollback(ota_boot_control_record_t *record)
{
    uint32_t rejected_slot = record->pending_slot;

    if(record->active_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE ||
       !ota_boot_v2_slot_descriptor_is_installable(record, record->active_slot))
    {
        return ota_boot_v2_enter_recovery(
            record, (uint32_t)OTA_CONTROL_ERROR_ROLLBACK_FAILED);
    }

    if(!ota_boot_v2_internal_matches_slot(record, record->active_slot) &&
       !ota_boot_v2_install_slot(record, record->active_slot))
    {
        return ota_boot_v2_enter_recovery(
            record, (uint32_t)OTA_CONTROL_ERROR_ROLLBACK_FAILED);
    }

    if(rejected_slot == (uint32_t)OTA_FIRMWARE_SLOT_A ||
       rejected_slot == (uint32_t)OTA_FIRMWARE_SLOT_B)
    {
        record->slots[rejected_slot].state = (uint32_t)OTA_SLOT_STATE_REJECTED;
    }
    record->pending_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    record->state = (uint32_t)OTA_CONTROL_STATE_CONFIRMED;
    record->trial_boot_count = 0U;
    if(record->last_error == (uint32_t)OTA_CONTROL_ERROR_NONE)
    {
        record->last_error = (uint32_t)OTA_CONTROL_ERROR_TRIAL_RESET;
    }
    record->last_error_address = 0U;

    return ota_boot_v2_store(record) ?
           OTA_BOOT_RESULT_ROLLED_BACK : OTA_BOOT_RESULT_ROLLBACK_FAILED;
}

uint8_t ota_boot_v2_record_available(void)
{
    ota_boot_control_record_t record;
    return ota_boot_v2_load(&record);
}

ota_boot_result_t ota_boot_v2_process(void)
{
    ota_boot_control_record_t record;

    if(!ota_boot_v2_load(&record))
    {
        return OTA_BOOT_RESULT_NO_UPDATE;
    }

    record.last_reset_reason = ota_boot_reset_reason();
    record.boot_count++;

    switch(record.state)
    {
    case OTA_CONTROL_STATE_CONFIRMED:
        if(record.active_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE)
        {
            return ota_boot_v2_enter_recovery(
                &record, (uint32_t)OTA_CONTROL_ERROR_BAD_RECORD);
        }
        if(ota_boot_v2_internal_matches_slot(&record, record.active_slot))
        {
            return OTA_BOOT_RESULT_NO_UPDATE;
        }
        if(ota_boot_v2_install_slot(&record, record.active_slot))
        {
            return OTA_BOOT_RESULT_INSTALLED;
        }
        return ota_boot_v2_enter_recovery(
            &record, (uint32_t)OTA_CONTROL_ERROR_INTERNAL_VERIFY);

    case OTA_CONTROL_STATE_DOWNLOADING:
    case OTA_CONTROL_STATE_VERIFIED:
        return OTA_BOOT_RESULT_NO_UPDATE;

    case OTA_CONTROL_STATE_PENDING:
        record.state = (uint32_t)OTA_CONTROL_STATE_INSTALLING;
        record.last_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
        if(!ota_boot_v2_store(&record))
        {
            return OTA_BOOT_RESULT_FLASH_ERROR;
        }
        /* Continue installation using the newly committed INSTALLING record. */
        /* fall through */

    case OTA_CONTROL_STATE_INSTALLING:
        if(record.pending_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE ||
           !ota_boot_v2_install_slot(&record, record.pending_slot))
        {
            record.state = (uint32_t)OTA_CONTROL_STATE_ROLLBACK;
            if(record.last_error == (uint32_t)OTA_CONTROL_ERROR_NONE)
            {
                record.last_error = (uint32_t)OTA_CONTROL_ERROR_INTERNAL_PROGRAM;
            }
            (void)ota_boot_v2_store(&record);
            return ota_boot_v2_rollback(&record);
        }
        record.state = (uint32_t)OTA_CONTROL_STATE_TRIAL;
        record.trial_boot_count = 0U;
        record.last_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
        return ota_boot_v2_store(&record) ?
               OTA_BOOT_RESULT_INSTALLED : OTA_BOOT_RESULT_FLASH_ERROR;

    case OTA_CONTROL_STATE_TRIAL:
        if(record.pending_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE ||
           !ota_boot_v2_internal_matches_slot(&record, record.pending_slot))
        {
            record.state = (uint32_t)OTA_CONTROL_STATE_ROLLBACK;
            record.last_error = (uint32_t)OTA_CONTROL_ERROR_TRIAL_RESET;
            (void)ota_boot_v2_store(&record);
            return ota_boot_v2_rollback(&record);
        }

        record.trial_boot_count++;
        if(record.trial_boot_count >= record.trial_boot_limit)
        {
            record.state = (uint32_t)OTA_CONTROL_STATE_ROLLBACK;
            record.last_error = (uint32_t)OTA_CONTROL_ERROR_TRIAL_TIMEOUT;
            (void)ota_boot_v2_store(&record);
            return ota_boot_v2_rollback(&record);
        }

        return ota_boot_v2_store(&record) ?
               OTA_BOOT_RESULT_NO_UPDATE : OTA_BOOT_RESULT_FLASH_ERROR;

    case OTA_CONTROL_STATE_ROLLBACK:
        return ota_boot_v2_rollback(&record);

    case OTA_CONTROL_STATE_RECOVERY:
        return OTA_BOOT_RESULT_RECOVERY_REQUIRED;

    default:
        return ota_boot_v2_enter_recovery(
            &record, (uint32_t)OTA_CONTROL_ERROR_BAD_RECORD);
    }
}

uint8_t ota_boot_v2_migrate_confirmed_v1(const ota_manifest_t *manifest)
{
    ota_boot_control_record_t record;
    uint32_t active_slot;

    if(manifest == NULL ||
       !ota_boot_v2_address_to_slot(manifest->package_address, &active_slot) ||
       !ota_boot_external_image_is_valid(
           manifest->package_address, manifest->image_size, manifest->image_crc32) ||
       !ota_boot_internal_image_matches(manifest->image_size, manifest->image_crc32))
    {
        return 0U;
    }

    ota_boot_control_init(&record);
    record.state = (uint32_t)OTA_CONTROL_STATE_CONFIRMED;
    record.active_slot = active_slot;
    ota_boot_v2_descriptor_from_v1(
        &record.slots[active_slot], manifest, (uint32_t)OTA_SLOT_STATE_CONFIRMED);
    return ota_boot_v2_store(&record);
}

uint8_t ota_boot_v2_migrate_trial_v1(const ota_manifest_t *manifest)
{
    ota_boot_control_record_t record;
    ota_firmware_descriptor_t *active_descriptor;
    uint32_t pending_slot;
    uint32_t active_slot;
    uint32_t active_entry;

    if(manifest == NULL ||
       !ota_boot_v2_address_to_slot(manifest->package_address, &pending_slot) ||
       !ota_boot_external_image_is_valid(
           manifest->package_address, manifest->image_size, manifest->image_crc32) ||
       !ota_boot_internal_image_matches(manifest->image_size, manifest->image_crc32))
    {
        return 0U;
    }

    ota_boot_control_init(&record);
    record.state = (uint32_t)OTA_CONTROL_STATE_TRIAL;
    record.pending_slot = pending_slot;
    ota_boot_v2_descriptor_from_v1(
        &record.slots[pending_slot], manifest, (uint32_t)OTA_SLOT_STATE_VERIFIED);

    if(ota_boot_v2_address_to_slot(manifest->rollback_address, &active_slot) &&
       manifest->rollback_size != 0U &&
       ota_boot_external_image_is_valid(
           manifest->rollback_address,
           manifest->rollback_size,
           manifest->rollback_crc32) &&
       ota_boot_v2_external_vector(
           manifest->rollback_address,
           manifest->rollback_size,
           &active_entry))
    {
        record.active_slot = active_slot;
        active_descriptor = &record.slots[active_slot];
        active_descriptor->state = (uint32_t)OTA_SLOT_STATE_CONFIRMED;
        active_descriptor->image_version =
            (manifest->image_version > 0U) ? manifest->image_version - 1U : 0U;
        active_descriptor->image_size = manifest->rollback_size;
        active_descriptor->image_crc32 = manifest->rollback_crc32;
        active_descriptor->load_address = OTA_APP_BASE;
        active_descriptor->entry_address = active_entry;
    }

    return ota_boot_v2_store(&record);
}
