/**
 * @file app_firmware_update_service.c
 * @brief Shared A/B firmware transaction binding for App transports.
 *
 * BSP_FLASH application adapter for the shared firmware A/B transaction.
 *
 * The GD25 driver owns SPI serialization. This module owns one update context
 * and exposes it to transports without exposing absolute firmware addresses.
 */
#include "app_firmware_update_service.h"
#include "app_board_io.h"
#include "bsp_flash.h"

static ota_firmware_update_t firmware_update;
static uint8_t is_initialized;

static uint8_t app_firmware_update_read(
    void *context,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    (void)context;
    return bsp_flash_read(address, data, size);
}

static uint8_t app_firmware_update_erase_sector(void *context, uint32_t address)
{
    (void)context;
    return bsp_flash_erase_4k(address);
}

static uint8_t app_firmware_update_write(
    void *context,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    (void)context;
    return bsp_flash_write(address, data, size);
}

ota_firmware_update_status_t app_firmware_update_service_init(void)
{
    ota_firmware_update_storage_t storage;
    ota_firmware_update_status_t status;

    if(is_initialized != 0U)
    {
        return OTA_FIRMWARE_UPDATE_OK;
    }

    storage.context = NULL;
    storage.read = app_firmware_update_read;
    storage.erase_sector = app_firmware_update_erase_sector;
    storage.write = app_firmware_update_write;
    status = ota_firmware_update_init(&firmware_update, &storage);
    if(status == OTA_FIRMWARE_UPDATE_OK)
    {
        is_initialized = 1U;
    }

    return status;
}

ota_firmware_update_status_t app_firmware_update_service_begin(
    const ota_firmware_descriptor_t *descriptor)
{
    ota_firmware_update_status_t status = app_firmware_update_service_init();

    return (status == OTA_FIRMWARE_UPDATE_OK) ?
           ota_firmware_update_begin(&firmware_update, descriptor) : status;
}

ota_firmware_update_status_t app_firmware_update_service_write(
    uint32_t offset,
    const uint8_t *data,
    uint32_t size)
{
    if(is_initialized == 0U)
    {
        return OTA_FIRMWARE_UPDATE_BAD_STATE;
    }

    return ota_firmware_update_write(&firmware_update, offset, data, size);
}

ota_firmware_update_status_t app_firmware_update_service_finish(void)
{
    if(is_initialized == 0U)
    {
        return OTA_FIRMWARE_UPDATE_BAD_STATE;
    }

    return ota_firmware_update_finish(&firmware_update);
}

ota_firmware_update_status_t app_firmware_update_service_abort(void)
{
    if(is_initialized == 0U)
    {
        return OTA_FIRMWARE_UPDATE_BAD_STATE;
    }

    return ota_firmware_update_abort(&firmware_update);
}

void app_firmware_update_service_get_progress(
    uint8_t *is_active,
    uint32_t *target_slot,
    uint32_t *received_size,
    uint32_t *expected_size)
{
    if(is_active != NULL)
    {
        *is_active = firmware_update.is_active;
    }
    if(target_slot != NULL)
    {
        *target_slot = firmware_update.target_slot;
    }
    if(received_size != NULL)
    {
        *received_size = firmware_update.received_size;
    }
    if(expected_size != NULL)
    {
        *expected_size = firmware_update.expected_size;
    }
}
