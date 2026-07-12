/*
 * Shared OTA boot-control record validation and two-sector commit logic.
 *
 * This module has no HAL or RTOS dependency. Hardware integrations provide
 * bounded read, sector-erase and write callbacks through the storage interface.
 */
#include "ota_boot_control.h"
#include <string.h>

typedef char ota_boot_control_record_size_must_be_348_bytes[
    (sizeof(ota_boot_control_record_t) == 348U) ? 1 : -1];

static uint32_t ota_boot_control_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    crc = ~crc;

    while(size-- != 0U)
    {
        uint32_t bit;

        crc ^= *data++;
        for(bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                  ((crc >> 1U) ^ 0xEDB88320UL) :
                  (crc >> 1U);
        }
    }

    return ~crc;
}

static uint32_t ota_boot_control_record_crc32(const ota_boot_control_record_t *record)
{
    return ota_boot_control_crc32_update(
        0U,
        (const uint8_t *)record,
        (uint32_t)offsetof(ota_boot_control_record_t, record_crc32));
}

static uint8_t ota_boot_control_slot_id_is_valid(uint32_t slot, uint8_t allow_none)
{
    if(slot == (uint32_t)OTA_FIRMWARE_SLOT_A || slot == (uint32_t)OTA_FIRMWARE_SLOT_B)
    {
        return 1U;
    }

    return (allow_none != 0U && slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE) ? 1U : 0U;
}

static uint8_t ota_boot_control_descriptor_is_valid(const ota_firmware_descriptor_t *descriptor)
{
    if(descriptor->state > (uint32_t)OTA_SLOT_STATE_REJECTED)
    {
        return 0U;
    }

    if(descriptor->state == (uint32_t)OTA_SLOT_STATE_EMPTY)
    {
        return 1U;
    }

    if(descriptor->state == (uint32_t)OTA_SLOT_STATE_DOWNLOADING)
    {
        return (descriptor->image_size <= OTA_EXT_FIRMWARE_SLOT_SIZE) ? 1U : 0U;
    }

    if(descriptor->image_size == 0U || descriptor->image_size > OTA_EXT_FIRMWARE_SLOT_SIZE)
    {
        return 0U;
    }

    if(descriptor->load_address != OTA_APP_BASE ||
       descriptor->entry_address < OTA_APP_BASE ||
       descriptor->entry_address >= (OTA_APP_BASE + OTA_APP_SIZE) ||
       (descriptor->entry_address & 1U) == 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ota_boot_control_state_is_valid(const ota_boot_control_record_t *record)
{
    if(record->state > (uint32_t)OTA_CONTROL_STATE_RECOVERY ||
       record->trial_boot_limit == 0U ||
       record->trial_boot_count > record->trial_boot_limit ||
       !ota_boot_control_slot_id_is_valid(record->active_slot, 1U) ||
       !ota_boot_control_slot_id_is_valid(record->pending_slot, 1U))
    {
        return 0U;
    }

    if(record->active_slot != (uint32_t)OTA_FIRMWARE_SLOT_NONE &&
       record->active_slot == record->pending_slot)
    {
        return 0U;
    }

    if(record->state == (uint32_t)OTA_CONTROL_STATE_EMPTY)
    {
        return (record->active_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE &&
                record->pending_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE) ? 1U : 0U;
    }

    if(record->state == (uint32_t)OTA_CONTROL_STATE_CONFIRMED)
    {
        if(record->active_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE ||
           record->pending_slot != (uint32_t)OTA_FIRMWARE_SLOT_NONE)
        {
            return 0U;
        }

        return (record->slots[record->active_slot].state ==
                (uint32_t)OTA_SLOT_STATE_CONFIRMED) ? 1U : 0U;
    }

    if(record->state >= (uint32_t)OTA_CONTROL_STATE_DOWNLOADING &&
       record->state <= (uint32_t)OTA_CONTROL_STATE_TRIAL)
    {
        return (record->pending_slot != (uint32_t)OTA_FIRMWARE_SLOT_NONE) ? 1U : 0U;
    }

    return 1U;
}

static uint8_t ota_boot_control_sequence_is_newer(uint32_t candidate, uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1U : 0U;
}

static uint8_t ota_boot_control_storage_is_valid(const ota_boot_control_storage_t *storage)
{
    return (storage != NULL && storage->read != NULL &&
            storage->erase_sector != NULL && storage->write != NULL) ? 1U : 0U;
}

static uint32_t ota_boot_control_copy_address(ota_control_copy_t copy)
{
    return (copy == OTA_CONTROL_COPY_A) ?
           OTA_EXT_MANIFEST_A_ADDR : OTA_EXT_MANIFEST_B_ADDR;
}

void ota_boot_control_init(ota_boot_control_record_t *record)
{
    if(record == NULL)
    {
        return;
    }

    memset(record, 0, sizeof(*record));
    record->state = (uint32_t)OTA_CONTROL_STATE_EMPTY;
    record->active_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    record->pending_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    record->trial_boot_limit = OTA_BOOT_CONTROL_DEFAULT_TRIAL_LIMIT;
    ota_boot_control_prepare(record);
}

void ota_boot_control_prepare(ota_boot_control_record_t *record)
{
    if(record == NULL)
    {
        return;
    }

    record->magic = OTA_BOOT_CONTROL_MAGIC;
    record->schema_version = OTA_BOOT_CONTROL_SCHEMA_VERSION;
    record->record_size = (uint32_t)sizeof(*record);
    record->record_crc32 = ota_boot_control_record_crc32(record);
    record->commit_marker = OTA_BOOT_CONTROL_COMMIT_ERASED;
}

void ota_boot_control_mark_committed(ota_boot_control_record_t *record)
{
    if(record != NULL)
    {
        record->commit_marker = OTA_BOOT_CONTROL_COMMIT_VALUE;
    }
}

uint8_t ota_boot_control_body_is_valid(const ota_boot_control_record_t *record)
{
    if(record == NULL ||
       record->magic != OTA_BOOT_CONTROL_MAGIC ||
       record->schema_version != OTA_BOOT_CONTROL_SCHEMA_VERSION ||
       record->record_size != (uint32_t)sizeof(*record) ||
       record->record_crc32 != ota_boot_control_record_crc32(record) ||
       !ota_boot_control_descriptor_is_valid(&record->slots[OTA_FIRMWARE_SLOT_A]) ||
       !ota_boot_control_descriptor_is_valid(&record->slots[OTA_FIRMWARE_SLOT_B]) ||
       !ota_boot_control_state_is_valid(record))
    {
        return 0U;
    }

    return 1U;
}

uint8_t ota_boot_control_is_valid(const ota_boot_control_record_t *record)
{
    return (ota_boot_control_body_is_valid(record) != 0U &&
            record->commit_marker == OTA_BOOT_CONTROL_COMMIT_VALUE) ? 1U : 0U;
}

ota_control_status_t ota_boot_control_select(
    const ota_boot_control_record_t *copy_a,
    const ota_boot_control_record_t *copy_b,
    ota_boot_control_record_t *selected,
    ota_control_copy_t *selected_copy)
{
    uint8_t valid_a;
    uint8_t valid_b;

    if(copy_a == NULL || copy_b == NULL || selected == NULL || selected_copy == NULL)
    {
        return OTA_CONTROL_STATUS_INVALID_ARGUMENT;
    }

    valid_a = ota_boot_control_is_valid(copy_a);
    valid_b = ota_boot_control_is_valid(copy_b);

    if(valid_a == 0U && valid_b == 0U)
    {
        *selected_copy = OTA_CONTROL_COPY_NONE;
        return OTA_CONTROL_STATUS_NO_VALID_RECORD;
    }

    if(valid_b != 0U &&
       (valid_a == 0U || ota_boot_control_sequence_is_newer(copy_b->sequence, copy_a->sequence) != 0U))
    {
        *selected = *copy_b;
        *selected_copy = OTA_CONTROL_COPY_B;
    }
    else
    {
        *selected = *copy_a;
        *selected_copy = OTA_CONTROL_COPY_A;
    }

    return OTA_CONTROL_STATUS_OK;
}

uint8_t ota_boot_control_transition_is_allowed(uint32_t from_state, uint32_t to_state)
{
    switch(from_state)
    {
        case OTA_CONTROL_STATE_EMPTY:
            return (to_state == OTA_CONTROL_STATE_DOWNLOADING ||
                    to_state == OTA_CONTROL_STATE_RECOVERY) ? 1U : 0U;

        case OTA_CONTROL_STATE_CONFIRMED:
            return (to_state == OTA_CONTROL_STATE_DOWNLOADING ||
                    to_state == OTA_CONTROL_STATE_RECOVERY) ? 1U : 0U;

        case OTA_CONTROL_STATE_DOWNLOADING:
            return (to_state == OTA_CONTROL_STATE_VERIFIED ||
                    to_state == OTA_CONTROL_STATE_CONFIRMED ||
                    to_state == OTA_CONTROL_STATE_RECOVERY) ? 1U : 0U;

        case OTA_CONTROL_STATE_VERIFIED:
            return (to_state == OTA_CONTROL_STATE_PENDING ||
                    to_state == OTA_CONTROL_STATE_CONFIRMED ||
                    to_state == OTA_CONTROL_STATE_RECOVERY) ? 1U : 0U;

        case OTA_CONTROL_STATE_PENDING:
            return (to_state == OTA_CONTROL_STATE_INSTALLING ||
                    to_state == OTA_CONTROL_STATE_ROLLBACK) ? 1U : 0U;

        case OTA_CONTROL_STATE_INSTALLING:
            return (to_state == OTA_CONTROL_STATE_TRIAL ||
                    to_state == OTA_CONTROL_STATE_ROLLBACK) ? 1U : 0U;

        case OTA_CONTROL_STATE_TRIAL:
            return (to_state == OTA_CONTROL_STATE_CONFIRMED ||
                    to_state == OTA_CONTROL_STATE_ROLLBACK) ? 1U : 0U;

        case OTA_CONTROL_STATE_ROLLBACK:
            return (to_state == OTA_CONTROL_STATE_CONFIRMED ||
                    to_state == OTA_CONTROL_STATE_RECOVERY) ? 1U : 0U;

        case OTA_CONTROL_STATE_RECOVERY:
            return (to_state == OTA_CONTROL_STATE_DOWNLOADING ||
                    to_state == OTA_CONTROL_STATE_PENDING) ? 1U : 0U;

        default:
            return 0U;
    }
}

ota_control_status_t ota_boot_control_storage_load(
    const ota_boot_control_storage_t *storage,
    ota_boot_control_record_t *record,
    ota_control_copy_t *source_copy)
{
    ota_boot_control_record_t copy_a;
    ota_boot_control_record_t copy_b;
    ota_control_status_t select_status;
    uint8_t read_a;
    uint8_t read_b;

    if(!ota_boot_control_storage_is_valid(storage) || record == NULL || source_copy == NULL)
    {
        return OTA_CONTROL_STATUS_INVALID_ARGUMENT;
    }

    memset(&copy_a, 0xFF, sizeof(copy_a));
    memset(&copy_b, 0xFF, sizeof(copy_b));
    read_a = storage->read(storage->context, OTA_EXT_MANIFEST_A_ADDR,
                           (uint8_t *)&copy_a, (uint32_t)sizeof(copy_a));
    read_b = storage->read(storage->context, OTA_EXT_MANIFEST_B_ADDR,
                           (uint8_t *)&copy_b, (uint32_t)sizeof(copy_b));

    if(read_a == 0U && read_b == 0U)
    {
        return OTA_CONTROL_STATUS_IO_ERROR;
    }

    select_status = ota_boot_control_select(&copy_a, &copy_b, record, source_copy);
    if(select_status == OTA_CONTROL_STATUS_NO_VALID_RECORD &&
       (read_a == 0U || read_b == 0U))
    {
        return OTA_CONTROL_STATUS_IO_ERROR;
    }

    return select_status;
}

ota_control_status_t ota_boot_control_storage_store(
    const ota_boot_control_storage_t *storage,
    const ota_boot_control_record_t *requested,
    ota_boot_control_record_t *committed,
    ota_control_copy_t *committed_copy)
{
    ota_boot_control_record_t current;
    ota_boot_control_record_t candidate;
    ota_boot_control_record_t verify;
    ota_control_copy_t current_copy = OTA_CONTROL_COPY_NONE;
    ota_control_copy_t target_copy;
    ota_control_status_t load_status;
    uint32_t target_address;
    const uint32_t body_size = (uint32_t)offsetof(ota_boot_control_record_t, commit_marker);
    const uint32_t commit_offset = (uint32_t)offsetof(ota_boot_control_record_t, commit_marker);
    const uint32_t commit_value = OTA_BOOT_CONTROL_COMMIT_VALUE;

    if(!ota_boot_control_storage_is_valid(storage) || requested == NULL ||
       committed == NULL || committed_copy == NULL)
    {
        return OTA_CONTROL_STATUS_INVALID_ARGUMENT;
    }

    load_status = ota_boot_control_storage_load(storage, &current, &current_copy);
    if(load_status != OTA_CONTROL_STATUS_OK && load_status != OTA_CONTROL_STATUS_NO_VALID_RECORD)
    {
        return load_status;
    }

    candidate = *requested;
    candidate.sequence = (load_status == OTA_CONTROL_STATUS_OK) ? current.sequence + 1U : 1U;
    ota_boot_control_prepare(&candidate);

    if(!ota_boot_control_body_is_valid(&candidate))
    {
        return OTA_CONTROL_STATUS_BAD_RECORD;
    }

    target_copy = (current_copy == OTA_CONTROL_COPY_A) ? OTA_CONTROL_COPY_B : OTA_CONTROL_COPY_A;
    target_address = ota_boot_control_copy_address(target_copy);

    if(!storage->erase_sector(storage->context, target_address) ||
       !storage->write(storage->context, target_address,
                       (const uint8_t *)&candidate, body_size) ||
       !storage->read(storage->context, target_address,
                      (uint8_t *)&verify, (uint32_t)sizeof(verify)))
    {
        return OTA_CONTROL_STATUS_IO_ERROR;
    }

    if(!ota_boot_control_body_is_valid(&verify) ||
       verify.commit_marker != OTA_BOOT_CONTROL_COMMIT_ERASED)
    {
        return OTA_CONTROL_STATUS_VERIFY_FAILED;
    }

    if(!storage->write(storage->context, target_address + commit_offset,
                       (const uint8_t *)&commit_value, (uint32_t)sizeof(commit_value)) ||
       !storage->read(storage->context, target_address,
                      (uint8_t *)&verify, (uint32_t)sizeof(verify)))
    {
        return OTA_CONTROL_STATUS_IO_ERROR;
    }

    if(!ota_boot_control_is_valid(&verify))
    {
        return OTA_CONTROL_STATUS_VERIFY_FAILED;
    }

    *committed = verify;
    *committed_copy = target_copy;
    return OTA_CONTROL_STATUS_OK;
}
