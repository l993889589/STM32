#include "app_ota.h"
#include "app_board_io.h"
#include "app_health.h"
#include "../../../shared/ota/ota_boot_control.h"
#include "gd25lq128.h"
#include "main.h"
#include "ota_layout.h"
#include <string.h>

#define APP_ENABLE_OTA_LOG 0U

static void app_ota_log(const char *line)
{
#if APP_ENABLE_OTA_LOG
    if(line)
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
#else
    (void)line;
#endif
}

static uint32_t app_ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;

    while(len-- != 0U)
    {
        crc ^= *data++;
        for(uint32_t bit = 0U; bit < 8U; bit++)
        {
            if((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

static uint32_t app_ota_manifest_crc32(const ota_manifest_t *manifest)
{
    ota_manifest_t temp = *manifest;
    temp.manifest_crc32 = 0U;
    return app_ota_crc32_update(0U, (const uint8_t *)&temp, sizeof(temp));
}

static uint8_t app_ota_manifest_read(uint32_t address, ota_manifest_t *manifest)
{
    if(!gd25lq128_read(address, (uint8_t *)manifest, sizeof(*manifest)))
    {
        return 0U;
    }

    return (manifest->magic == OTA_MANIFEST_MAGIC) &&
           (manifest->version == OTA_MANIFEST_VERSION) &&
           (manifest->manifest_crc32 == app_ota_manifest_crc32(manifest));
}

static uint8_t app_ota_manifest_write(uint32_t address, const ota_manifest_t *manifest)
{
    ota_manifest_t temp = *manifest;
    temp.manifest_crc32 = app_ota_manifest_crc32(&temp);

    return gd25lq128_erase_4k(address) &&
           gd25lq128_write(address, (const uint8_t *)&temp, sizeof(temp)) &&
           gd25lq128_read_verify(address, (const uint8_t *)&temp, sizeof(temp));
}

static uint8_t app_ota_manifest_load_active(ota_manifest_t *manifest)
{
    ota_manifest_t manifest_a;
    ota_manifest_t manifest_b;
    const uint8_t ok_a = app_ota_manifest_read(OTA_EXT_MANIFEST_A_ADDR, &manifest_a);
    const uint8_t ok_b = app_ota_manifest_read(OTA_EXT_MANIFEST_B_ADDR, &manifest_b);

    if(ok_a && ok_b)
    {
        *manifest = (manifest_b.image_version >= manifest_a.image_version) ? manifest_b : manifest_a;
        return 1U;
    }

    if(ok_a)
    {
        *manifest = manifest_a;
        return 1U;
    }

    if(ok_b)
    {
        *manifest = manifest_b;
        return 1U;
    }

    return 0U;
}

static uint8_t app_ota_manifest_store_both(const ota_manifest_t *manifest)
{
    return app_ota_manifest_write(OTA_EXT_MANIFEST_A_ADDR, manifest) &&
           app_ota_manifest_write(OTA_EXT_MANIFEST_B_ADDR, manifest);
}

static uint8_t app_ota_control_read(
    void *context,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    (void)context;
    return gd25lq128_read(address, data, size);
}

static uint8_t app_ota_control_erase(void *context, uint32_t address)
{
    (void)context;
    return gd25lq128_erase_4k(address);
}

static uint8_t app_ota_control_write(
    void *context,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    (void)context;
    return gd25lq128_write(address, data, size);
}

/* Return -1 when no v2 record exists, 0 on failure, and 1 on success. */
static int app_ota_confirm_v2_trial(void)
{
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t record;
    ota_boot_control_record_t committed;
    ota_control_copy_t copy;
    ota_control_status_t status;
    uint32_t pending_slot;

    storage.context = NULL;
    storage.read = app_ota_control_read;
    storage.erase_sector = app_ota_control_erase;
    storage.write = app_ota_control_write;

    status = ota_boot_control_storage_load(&storage, &record, &copy);
    if(status == OTA_CONTROL_STATUS_NO_VALID_RECORD)
    {
        return -1;
    }
    if(status != OTA_CONTROL_STATUS_OK)
    {
        return 0;
    }
    if(record.state != (uint32_t)OTA_CONTROL_STATE_TRIAL)
    {
        return 1;
    }

    pending_slot = record.pending_slot;
    if(pending_slot != (uint32_t)OTA_FIRMWARE_SLOT_A &&
       pending_slot != (uint32_t)OTA_FIRMWARE_SLOT_B)
    {
        return 0;
    }

    if(record.active_slot == (uint32_t)OTA_FIRMWARE_SLOT_A ||
       record.active_slot == (uint32_t)OTA_FIRMWARE_SLOT_B)
    {
        record.slots[record.active_slot].state = (uint32_t)OTA_SLOT_STATE_VERIFIED;
    }
    record.slots[pending_slot].state = (uint32_t)OTA_SLOT_STATE_CONFIRMED;
    if(record.minimum_version < record.slots[pending_slot].image_version)
    {
        record.minimum_version = record.slots[pending_slot].image_version;
    }
    record.active_slot = pending_slot;
    record.pending_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    record.state = (uint32_t)OTA_CONTROL_STATE_CONFIRMED;
    record.trial_boot_count = 0U;
    record.last_error = (uint32_t)OTA_CONTROL_ERROR_NONE;
    record.last_error_address = 0U;

    return (ota_boot_control_storage_store(
                &storage, &record, &committed, &copy) == OTA_CONTROL_STATUS_OK) ? 1 : 0;
}

uint8_t app_ota_confirm_trial_boot(void)
{
    ota_manifest_t manifest;
    int v2_result = app_ota_confirm_v2_trial();

    if(v2_result >= 0)
    {
        return (v2_result != 0) ? 1U : 0U;
    }

    if(!app_ota_manifest_load_active(&manifest))
    {
        return 0U;
    }

    if(manifest.boot_state != OTA_BOOT_STATE_TRIAL_BOOT)
    {
        return 1U;
    }

    manifest.boot_state = OTA_BOOT_STATE_CONFIRMED;
    return app_ota_manifest_store_both(&manifest);
}

void app_ota_confirm_task_entry(ULONG thread_input)
{
    ULONG started_at = tx_time_get();
    app_health_status_t health;

    (void)thread_input;

    for(;;)
    {
        if(app_health_is_ready(
               APP_OTA_HEALTH_OBSERVATION_TICKS,
               APP_OTA_HEARTBEAT_STALE_TICKS,
               &health))
        {
            app_ota_log("ota confirm: health gate passed\r\n");
            if(app_ota_confirm_trial_boot())
                app_ota_log("ota confirm: ok\r\n");
            else
                app_ota_log("ota confirm: failed\r\n");
            break;
        }

        if((ULONG)(tx_time_get() - started_at) >= APP_OTA_CONFIRM_DEADLINE_TICKS)
        {
            app_ota_log("ota confirm: health deadline reset\r\n");
            tx_thread_sleep(20U);
            NVIC_SystemReset();
        }

        tx_thread_sleep(APP_OTA_HEALTH_POLL_TICKS);
    }

    for(;;)
        tx_thread_sleep(TX_WAIT_FOREVER);
}
