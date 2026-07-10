/*
 * Shared external firmware A/B transaction implementation.
 *
 * The module is synchronous and bounded per call. Long erase and transfer work
 * is driven by the caller's task; no callback executes from interrupt context.
 */
#include "ota_firmware_update.h"
#include "ota_sha256.h"
#include <string.h>

static uint32_t ota_firmware_update_crc32(uint32_t crc, const uint8_t *data, uint32_t size)
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

static uint8_t ota_firmware_update_storage_is_valid(
    const ota_firmware_update_storage_t *storage)
{
    return (storage != NULL && storage->read != NULL &&
            storage->erase_sector != NULL && storage->write != NULL) ? 1U : 0U;
}

static uint8_t ota_firmware_update_descriptor_is_valid(
    const ota_firmware_descriptor_t *descriptor)
{
    return (descriptor != NULL &&
            descriptor->image_version != 0U &&
            descriptor->image_size != 0U &&
            descriptor->image_size <= OTA_EXT_FIRMWARE_SLOT_SIZE &&
            descriptor->load_address == OTA_APP_BASE &&
            descriptor->entry_address >= OTA_APP_BASE &&
            descriptor->entry_address < (OTA_APP_BASE + OTA_APP_SIZE) &&
            (descriptor->entry_address & 1U) != 0U) ? 1U : 0U;
}

static ota_boot_control_storage_t ota_firmware_update_control_storage(
    ota_firmware_update_t *update)
{
    ota_boot_control_storage_t storage;

    storage.context = update->storage.context;
    storage.read = update->storage.read;
    storage.erase_sector = update->storage.erase_sector;
    storage.write = update->storage.write;
    return storage;
}

static uint32_t ota_firmware_update_slot_address(uint32_t slot)
{
    return (slot == (uint32_t)OTA_FIRMWARE_SLOT_A) ?
           OTA_EXT_FIRMWARE_SLOT_A_ADDR : OTA_EXT_FIRMWARE_SLOT_B_ADDR;
}

static ota_firmware_update_status_t ota_firmware_update_store_control(
    ota_firmware_update_t *update)
{
    ota_boot_control_storage_t storage = ota_firmware_update_control_storage(update);
    ota_control_copy_t copy;
    ota_control_status_t status;

    status = ota_boot_control_storage_store(
        &storage, &update->control, &update->control, &copy);

    return (status == OTA_CONTROL_STATUS_OK) ?
           OTA_FIRMWARE_UPDATE_OK : OTA_FIRMWARE_UPDATE_CONTROL_ERROR;
}

static ota_firmware_update_status_t ota_firmware_update_load_control(
    ota_firmware_update_t *update)
{
    ota_boot_control_storage_t storage = ota_firmware_update_control_storage(update);
    ota_control_copy_t copy;
    ota_control_status_t status;

    status = ota_boot_control_storage_load(&storage, &update->control, &copy);
    if(status == OTA_CONTROL_STATUS_NO_VALID_RECORD)
    {
        return OTA_FIRMWARE_UPDATE_NOT_PROVISIONED;
    }

    return (status == OTA_CONTROL_STATUS_OK) ?
           OTA_FIRMWARE_UPDATE_OK : OTA_FIRMWARE_UPDATE_CONTROL_ERROR;
}

static ota_firmware_update_status_t ota_firmware_update_erase_target(
    ota_firmware_update_t *update)
{
    uint32_t erase_size = (update->expected_size + OTA_EXT_SECTOR_SIZE - 1U) &
                          ~(OTA_EXT_SECTOR_SIZE - 1U);
    uint32_t offset;

    for(offset = 0U; offset < erase_size; offset += OTA_EXT_SECTOR_SIZE)
    {
        if(!update->storage.erase_sector(
               update->storage.context, update->target_address + offset))
        {
            return OTA_FIRMWARE_UPDATE_IO_ERROR;
        }
    }

    return OTA_FIRMWARE_UPDATE_OK;
}

ota_firmware_update_status_t ota_firmware_update_init(
    ota_firmware_update_t *update,
    const ota_firmware_update_storage_t *storage)
{
    if(update == NULL || !ota_firmware_update_storage_is_valid(storage))
    {
        return OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT;
    }

    memset(update, 0, sizeof(*update));
    update->storage = *storage;
    update->target_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    return OTA_FIRMWARE_UPDATE_OK;
}

static ota_firmware_update_status_t ota_firmware_update_begin_internal(
    ota_firmware_update_t *update,
    const ota_firmware_descriptor_t *descriptor,
    uint8_t recovery_mode)
{
    ota_firmware_update_status_t status;
    uint32_t target_slot;

    if(update == NULL || !ota_firmware_update_descriptor_is_valid(descriptor))
    {
        return OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT;
    }

    if(update->is_active != 0U)
    {
        return OTA_FIRMWARE_UPDATE_BUSY;
    }

    status = ota_firmware_update_load_control(update);
    if(status == OTA_FIRMWARE_UPDATE_NOT_PROVISIONED && recovery_mode != 0U)
    {
        ota_boot_control_init(&update->control);
        status = OTA_FIRMWARE_UPDATE_OK;
    }
    if(status != OTA_FIRMWARE_UPDATE_OK)
    {
        return status;
    }

    if(recovery_mode == 0U &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_CONFIRMED &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_DOWNLOADING &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_VERIFIED)
    {
        return OTA_FIRMWARE_UPDATE_BAD_STATE;
    }

    if(recovery_mode != 0U &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_EMPTY &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_CONFIRMED &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_DOWNLOADING &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_VERIFIED &&
       update->control.state != (uint32_t)OTA_CONTROL_STATE_RECOVERY)
    {
        return OTA_FIRMWARE_UPDATE_BAD_STATE;
    }

    if(recovery_mode == 0U &&
       update->control.active_slot != (uint32_t)OTA_FIRMWARE_SLOT_A &&
       update->control.active_slot != (uint32_t)OTA_FIRMWARE_SLOT_B)
    {
        return OTA_FIRMWARE_UPDATE_NOT_PROVISIONED;
    }

    target_slot = (update->control.active_slot == (uint32_t)OTA_FIRMWARE_SLOT_A) ?
                  (uint32_t)OTA_FIRMWARE_SLOT_B : (uint32_t)OTA_FIRMWARE_SLOT_A;
    update->abort_state = (update->control.state == (uint32_t)OTA_CONTROL_STATE_RECOVERY ||
                           update->control.active_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE) ?
                          (uint32_t)OTA_CONTROL_STATE_RECOVERY :
                          (uint32_t)OTA_CONTROL_STATE_CONFIRMED;

    update->control.state = (uint32_t)OTA_CONTROL_STATE_DOWNLOADING;
    update->control.pending_slot = target_slot;
    update->control.trial_boot_count = 0U;
    update->control.last_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
    update->control.last_error_address = 0U;
    update->control.slots[target_slot] = *descriptor;
    update->control.slots[target_slot].state = (uint32_t)OTA_SLOT_STATE_DOWNLOADING;

    status = ota_firmware_update_store_control(update);
    if(status != OTA_FIRMWARE_UPDATE_OK)
    {
        return status;
    }

    update->target_slot = target_slot;
    update->target_address = ota_firmware_update_slot_address(target_slot);
    update->expected_size = descriptor->image_size;
    update->expected_crc32 = descriptor->image_crc32;
    update->received_size = 0U;

    status = ota_firmware_update_erase_target(update);
    if(status != OTA_FIRMWARE_UPDATE_OK)
    {
        return status;
    }

    update->is_active = 1U;
    return OTA_FIRMWARE_UPDATE_OK;
}

ota_firmware_update_status_t ota_firmware_update_begin(
    ota_firmware_update_t *update,
    const ota_firmware_descriptor_t *descriptor)
{
    return ota_firmware_update_begin_internal(update, descriptor, 0U);
}

ota_firmware_update_status_t ota_firmware_update_begin_recovery(
    ota_firmware_update_t *update,
    const ota_firmware_descriptor_t *descriptor)
{
    return ota_firmware_update_begin_internal(update, descriptor, 1U);
}

ota_firmware_update_status_t ota_firmware_update_write(
    ota_firmware_update_t *update,
    uint32_t offset,
    const uint8_t *data,
    uint32_t size)
{
    uint32_t verified = 0U;

    if(update == NULL || data == NULL || size == 0U)
    {
        return OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT;
    }

    if(update->is_active == 0U)
    {
        return OTA_FIRMWARE_UPDATE_BAD_STATE;
    }

    if(offset != update->received_size)
    {
        return OTA_FIRMWARE_UPDATE_SEQUENCE;
    }

    if(offset > update->expected_size || size > (update->expected_size - offset))
    {
        return OTA_FIRMWARE_UPDATE_BAD_RANGE;
    }

    if(!update->storage.write(
           update->storage.context, update->target_address + offset, data, size))
    {
        return OTA_FIRMWARE_UPDATE_IO_ERROR;
    }

    while(verified < size)
    {
        uint32_t chunk = size - verified;
        if(chunk > sizeof(update->verify_buffer))
        {
            chunk = sizeof(update->verify_buffer);
        }

        if(!update->storage.read(
               update->storage.context,
               update->target_address + offset + verified,
               update->verify_buffer,
               chunk) ||
           memcmp(update->verify_buffer, &data[verified], chunk) != 0)
        {
            return OTA_FIRMWARE_UPDATE_VERIFY_FAILED;
        }

        verified += chunk;
    }

    update->received_size += size;
    return OTA_FIRMWARE_UPDATE_OK;
}

ota_firmware_update_status_t ota_firmware_update_finish(ota_firmware_update_t *update)
{
    ota_firmware_update_status_t status;
    ota_sha256_context_t sha256;
    uint8_t image_sha256[OTA_SHA256_DIGEST_SIZE];
    uint32_t crc = 0U;
    uint32_t offset = 0U;

    if(update == NULL)
    {
        return OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT;
    }

    if(update->is_active == 0U || update->received_size != update->expected_size)
    {
        return OTA_FIRMWARE_UPDATE_SEQUENCE;
    }

    ota_sha256_init(&sha256);
    while(offset < update->expected_size)
    {
        uint32_t chunk = update->expected_size - offset;
        if(chunk > sizeof(update->verify_buffer))
        {
            chunk = sizeof(update->verify_buffer);
        }

        if(!update->storage.read(
               update->storage.context,
               update->target_address + offset,
               update->verify_buffer,
               chunk))
        {
            return OTA_FIRMWARE_UPDATE_IO_ERROR;
        }

        crc = ota_firmware_update_crc32(crc, update->verify_buffer, chunk);
        ota_sha256_update(&sha256, update->verify_buffer, chunk);
        offset += chunk;
    }

    ota_sha256_finish(&sha256, image_sha256);

    if(crc != update->expected_crc32)
    {
        update->control.last_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_CRC;
        update->control.last_error_address = update->target_address;
        (void)ota_firmware_update_store_control(update);
        return OTA_FIRMWARE_UPDATE_CRC_MISMATCH;
    }

    if(memcmp(image_sha256,
              update->control.slots[update->target_slot].image_sha256,
              sizeof(image_sha256)) != 0)
    {
        update->control.last_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_SHA256;
        update->control.last_error_address = update->target_address;
        (void)ota_firmware_update_store_control(update);
        return OTA_FIRMWARE_UPDATE_SHA256_MISMATCH;
    }

    update->control.slots[update->target_slot].state = (uint32_t)OTA_SLOT_STATE_VERIFIED;
    update->control.state = (uint32_t)OTA_CONTROL_STATE_VERIFIED;
    status = ota_firmware_update_store_control(update);
    if(status != OTA_FIRMWARE_UPDATE_OK)
    {
        return status;
    }

    update->control.state = (uint32_t)OTA_CONTROL_STATE_PENDING;
    status = ota_firmware_update_store_control(update);
    if(status != OTA_FIRMWARE_UPDATE_OK)
    {
        return status;
    }

    update->is_active = 0U;
    return OTA_FIRMWARE_UPDATE_OK;
}

ota_firmware_update_status_t ota_firmware_update_abort(ota_firmware_update_t *update)
{
    ota_firmware_update_status_t status;

    if(update == NULL)
    {
        return OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT;
    }

    status = ota_firmware_update_load_control(update);
    if(status != OTA_FIRMWARE_UPDATE_OK)
    {
        return status;
    }

    if(update->control.pending_slot != (uint32_t)OTA_FIRMWARE_SLOT_NONE)
    {
        update->control.slots[update->control.pending_slot].state =
            (uint32_t)OTA_SLOT_STATE_REJECTED;
    }

    update->control.pending_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    update->control.state = (update->control.active_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE) ?
                            (uint32_t)OTA_CONTROL_STATE_RECOVERY :
                            ((update->abort_state == (uint32_t)OTA_CONTROL_STATE_RECOVERY) ?
                             (uint32_t)OTA_CONTROL_STATE_RECOVERY :
                             (uint32_t)OTA_CONTROL_STATE_CONFIRMED);
    update->control.last_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
    update->control.last_error_address = 0U;
    status = ota_firmware_update_store_control(update);
    if(status == OTA_FIRMWARE_UPDATE_OK)
    {
        update->is_active = 0U;
        update->target_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    }

    return status;
}
