#include "app_ota.h"
#include "app_board_io.h"
#include "gd25lq128.h"
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

uint8_t app_ota_confirm_trial_boot(void)
{
    ota_manifest_t manifest;

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
    (void)thread_input;

    tx_thread_sleep(APP_OTA_CONFIRM_DELAY_TICKS);

    app_ota_log("ota confirm: checking trial boot\r\n");
    if(app_ota_confirm_trial_boot())
        app_ota_log("ota confirm: ok\r\n");
    else
        app_ota_log("ota confirm: skipped or failed\r\n");

    for(;;)
        tx_thread_sleep(TX_WAIT_FOREVER);
}
