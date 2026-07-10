#include "ota_boot.h"
#include "ota_boot_private.h"
#include "ota_boot_v2.h"
#include "gd25lq128.h"
#include "main.h"
#include <string.h>

#define OTA_COPY_CHUNK_SIZE      256U
#define OTA_FLASH_QUADWORD_SIZE  16U

static uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
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

static uint32_t ota_manifest_crc32(const ota_manifest_t *manifest)
{
    ota_manifest_t temp = *manifest;
    temp.manifest_crc32 = 0U;
    return ota_crc32_update(0U, (const uint8_t *)&temp, sizeof(temp));
}

static uint8_t ota_manifest_read(uint32_t address, ota_manifest_t *manifest)
{
    if(!gd25lq128_read(address, (uint8_t *)manifest, sizeof(*manifest)))
    {
        return 0U;
    }

    if((manifest->magic != OTA_MANIFEST_MAGIC) ||
       (manifest->version != OTA_MANIFEST_VERSION) ||
       (manifest->manifest_crc32 != ota_manifest_crc32(manifest)))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ota_manifest_write(uint32_t address, const ota_manifest_t *manifest)
{
    ota_manifest_t temp = *manifest;
    temp.manifest_crc32 = ota_manifest_crc32(&temp);

    return gd25lq128_erase_4k(address) &&
           gd25lq128_write(address, (const uint8_t *)&temp, sizeof(temp)) &&
           gd25lq128_read_verify(address, (const uint8_t *)&temp, sizeof(temp));
}

static uint8_t ota_manifest_load_active(ota_manifest_t *manifest)
{
    ota_manifest_t manifest_a;
    ota_manifest_t manifest_b;
    const uint8_t ok_a = ota_manifest_read(OTA_EXT_MANIFEST_A_ADDR, &manifest_a);
    const uint8_t ok_b = ota_manifest_read(OTA_EXT_MANIFEST_B_ADDR, &manifest_b);

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

static uint8_t ota_manifest_store_both(const ota_manifest_t *manifest)
{
    return ota_manifest_write(OTA_EXT_MANIFEST_A_ADDR, manifest) &&
           ota_manifest_write(OTA_EXT_MANIFEST_B_ADDR, manifest);
}

static uint8_t ota_manifest_is_update_state(uint32_t state)
{
    return (state == OTA_BOOT_STATE_PENDING_UPDATE) ||
           (state == OTA_BOOT_STATE_INSTALLING);
}

static uint8_t ota_manifest_is_rollback_state(uint32_t state)
{
    return (state == OTA_BOOT_STATE_TRIAL_BOOT) ||
           (state == OTA_BOOT_STATE_ROLLBACK_REQUIRED) ||
           (state == OTA_BOOT_STATE_ROLLING_BACK);
}

static uint8_t ota_manifest_image_bounds_ok(const ota_manifest_t *manifest)
{
    const uint32_t package_end = manifest->package_address + manifest->package_size;

    if((manifest->image_size == 0U) || (manifest->image_size > OTA_APP_SIZE))
    {
        return 0U;
    }

    if((manifest->package_size == 0U) || (manifest->package_size < manifest->image_size))
    {
        return 0U;
    }

    if((manifest->package_address < OTA_EXT_DOWNLOAD_ADDR) ||
       (package_end < manifest->package_address) ||
       (package_end > (OTA_EXT_DOWNLOAD_ADDR + OTA_EXT_DOWNLOAD_SIZE)))
    {
        return 0U;
    }

    if(manifest->load_address != OTA_APP_BASE)
    {
        return 0U;
    }

    if((manifest->entry_address < OTA_APP_BASE) ||
       (manifest->entry_address >= (OTA_INTERNAL_FLASH_BASE + OTA_INTERNAL_FLASH_SIZE)) ||
       ((manifest->entry_address & 1U) == 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ota_verify_download_image(const ota_manifest_t *manifest)
{
    uint8_t buffer[OTA_COPY_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t crc = 0U;

    while(offset < manifest->image_size)
    {
        uint32_t chunk = manifest->image_size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if(!gd25lq128_read(manifest->package_address + offset, buffer, chunk))
        {
            return 0U;
        }

        crc = ota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }

    return (crc == manifest->image_crc32) ? 1U : 0U;
}

static uint8_t ota_app_vector_is_valid(void)
{
    const uint32_t app_sp = *(volatile uint32_t *)OTA_APP_BASE;
    const uint32_t app_reset = *(volatile uint32_t *)(OTA_APP_BASE + 4U);
    const uint32_t sram_end = OTA_SRAM_BASE + OTA_SRAM_SIZE;
    const uint32_t flash_end = OTA_INTERNAL_FLASH_BASE + OTA_INTERNAL_FLASH_SIZE;

    return (app_sp >= OTA_SRAM_BASE) &&
           (app_sp < sram_end) &&
           (app_reset >= OTA_APP_BASE) &&
           (app_reset < flash_end) &&
           ((app_reset & 1U) != 0U);
}

uint8_t ota_boot_app_is_valid(void)
{
    return ota_app_vector_is_valid();
}

uint32_t ota_boot_reset_reason(void)
{
    return RCC->RSR;
}

static uint8_t ota_quadword_is_blank(const uint8_t *data)
{
    for(uint32_t i = 0U; i < OTA_FLASH_QUADWORD_SIZE; i++)
    {
        if(data[i] != 0xFFU)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint32_t ota_current_app_used_size(void)
{
    uint8_t buffer[OTA_FLASH_QUADWORD_SIZE];
    uint32_t offset = OTA_APP_SIZE;

    if(!ota_app_vector_is_valid())
    {
        return 0U;
    }

    while(offset >= OTA_FLASH_QUADWORD_SIZE)
    {
        offset -= OTA_FLASH_QUADWORD_SIZE;
        memcpy(buffer, (const void *)(OTA_APP_BASE + offset), sizeof(buffer));
        if(!ota_quadword_is_blank(buffer))
        {
            return offset + OTA_FLASH_QUADWORD_SIZE;
        }
    }

    return 0U;
}

static uint32_t ota_crc32_internal_flash(uint32_t address, uint32_t size)
{
    uint8_t buffer[OTA_COPY_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t crc = 0U;

    while(offset < size)
    {
        uint32_t chunk = size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        memcpy(buffer, (const void *)(address + offset), chunk);
        crc = ota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }

    return crc;
}

static uint8_t ota_ext_erase_range(uint32_t address, uint32_t size)
{
    const uint32_t start = address & ~(OTA_EXT_SECTOR_SIZE - 1U);
    const uint32_t end = (address + size + OTA_EXT_SECTOR_SIZE - 1U) & ~(OTA_EXT_SECTOR_SIZE - 1U);

    if((size == 0U) || (end < address) || (end > OTA_EXT_FLASH_SIZE))
    {
        return 0U;
    }

    for(uint32_t pos = start; pos < end; pos += OTA_EXT_SECTOR_SIZE)
    {
        if(!gd25lq128_erase_4k(pos))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t ota_backup_current_app(ota_manifest_t *manifest)
{
    uint8_t buffer[OTA_COPY_CHUNK_SIZE];
    uint32_t offset = 0U;
    const uint32_t app_size = ota_current_app_used_size();

    manifest->rollback_address = OTA_EXT_BACKUP_ADDR;
    manifest->rollback_size = 0U;
    manifest->rollback_crc32 = 0U;

    if(app_size == 0U)
    {
        return 1U;
    }

    if(app_size > OTA_EXT_BACKUP_SIZE)
    {
        return 0U;
    }

    if(!ota_ext_erase_range(OTA_EXT_BACKUP_ADDR, app_size))
    {
        return 0U;
    }

    while(offset < app_size)
    {
        uint32_t chunk = app_size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        memcpy(buffer, (const void *)(OTA_APP_BASE + offset), chunk);
        if(!gd25lq128_write(OTA_EXT_BACKUP_ADDR + offset, buffer, chunk))
        {
            return 0U;
        }

        offset += chunk;
    }

    manifest->rollback_size = app_size;
    manifest->rollback_crc32 = ota_crc32_internal_flash(OTA_APP_BASE, app_size);

    return 1U;
}

static uint8_t ota_verify_ext_image(uint32_t address, uint32_t size, uint32_t expected_crc)
{
    uint8_t buffer[OTA_COPY_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t crc = 0U;

    if(size == 0U)
    {
        return 0U;
    }

    while(offset < size)
    {
        uint32_t chunk = size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        if(!gd25lq128_read(address + offset, buffer, chunk))
        {
            return 0U;
        }

        crc = ota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }

    return (crc == expected_crc) ? 1U : 0U;
}

static uint32_t ota_flash_bank_from_address(uint32_t address)
{
    return (address < (FLASH_BASE + FLASH_BANK_SIZE)) ? FLASH_BANK_1 : FLASH_BANK_2;
}

static uint32_t ota_flash_sector_from_address(uint32_t address)
{
    const uint32_t bank_base = (ota_flash_bank_from_address(address) == FLASH_BANK_1) ?
                              FLASH_BASE : (FLASH_BASE + FLASH_BANK_SIZE);

    return (address - bank_base) / FLASH_SECTOR_SIZE;
}

static uint8_t ota_flash_erase_app(uint32_t image_size)
{
    const uint32_t app_start = OTA_APP_BASE;
    const uint32_t app_end = OTA_APP_BASE + image_size;
    uint32_t current = app_start;

    while(current < app_end)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_error = 0xFFFFFFFFU;
        const uint32_t bank = ota_flash_bank_from_address(current);
        const uint32_t bank_end = (bank == FLASH_BANK_1) ?
                                  (FLASH_BASE + FLASH_BANK_SIZE) :
                                  (FLASH_BASE + FLASH_SIZE);
        const uint32_t erase_end = (app_end < bank_end) ? app_end : bank_end;
        const uint32_t first_sector = ota_flash_sector_from_address(current);
        const uint32_t last_sector = ota_flash_sector_from_address(erase_end - 1U);

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks = bank;
        erase.Sector = first_sector;
        erase.NbSectors = (last_sector - first_sector) + 1U;

        if(HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
        {
            return 0U;
        }

        current = erase_end;
    }

    return 1U;
}

static uint8_t ota_flash_program_app_from_ext(uint32_t source_address, uint32_t image_size)
{
    uint8_t buffer[OTA_COPY_CHUNK_SIZE];
    uint32_t offset = 0U;

    while(offset < image_size)
    {
        uint32_t chunk = image_size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        memset(buffer, 0xFF, sizeof(buffer));
        if(!gd25lq128_read(source_address + offset, buffer, chunk))
        {
            return 0U;
        }

        for(uint32_t pos = 0U; pos < chunk; pos += OTA_FLASH_QUADWORD_SIZE)
        {
            const uint32_t address = OTA_APP_BASE + offset + pos;
            if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, address, (uint32_t)&buffer[pos]) != HAL_OK)
            {
                return 0U;
            }
        }

        offset += chunk;
    }

    return 1U;
}

static uint8_t ota_flash_program_app(const ota_manifest_t *manifest)
{
    return ota_flash_program_app_from_ext(manifest->package_address, manifest->image_size);
}

static uint8_t ota_flash_verify_app(const ota_manifest_t *manifest)
{
    uint8_t buffer[OTA_COPY_CHUNK_SIZE];
    uint32_t offset = 0U;
    uint32_t crc = 0U;

    while(offset < manifest->image_size)
    {
        uint32_t chunk = manifest->image_size - offset;
        if(chunk > sizeof(buffer))
        {
            chunk = sizeof(buffer);
        }

        memcpy(buffer, (const void *)(OTA_APP_BASE + offset), chunk);
        crc = ota_crc32_update(crc, buffer, chunk);
        offset += chunk;
    }

    return (crc == manifest->image_crc32) ? 1U : 0U;
}

uint8_t ota_boot_internal_image_matches(uint32_t image_size, uint32_t image_crc32)
{
    return (image_size != 0U && image_size <= OTA_APP_SIZE &&
            ota_app_vector_is_valid() &&
            ota_crc32_internal_flash(OTA_APP_BASE, image_size) == image_crc32) ? 1U : 0U;
}

uint8_t ota_boot_external_image_is_valid(
    uint32_t address,
    uint32_t image_size,
    uint32_t image_crc32)
{
    uint32_t slot_end;

    if(address != OTA_EXT_FIRMWARE_SLOT_A_ADDR &&
       address != OTA_EXT_FIRMWARE_SLOT_B_ADDR)
    {
        return 0U;
    }

    slot_end = address + OTA_EXT_FIRMWARE_SLOT_SIZE;
    if(image_size == 0U || image_size > OTA_APP_SIZE ||
       image_size > OTA_EXT_FIRMWARE_SLOT_SIZE || address + image_size > slot_end)
    {
        return 0U;
    }

    return ota_verify_ext_image(address, image_size, image_crc32);
}

uint8_t ota_boot_install_external_image(
    uint32_t address,
    uint32_t image_size,
    uint32_t image_crc32)
{
    uint8_t programmed;

    if(!ota_boot_external_image_is_valid(address, image_size, image_crc32) ||
       HAL_FLASH_Unlock() != HAL_OK)
    {
        return 0U;
    }

    programmed = ota_flash_erase_app(OTA_APP_SIZE) &&
                 ota_flash_program_app_from_ext(address, image_size);
    (void)HAL_FLASH_Lock();

    return (programmed != 0U &&
            ota_boot_internal_image_matches(image_size, image_crc32)) ? 1U : 0U;
}

static uint8_t ota_restore_backup_app(ota_manifest_t *manifest)
{
    if((manifest->rollback_address != OTA_EXT_BACKUP_ADDR) ||
       (manifest->rollback_size == 0U) ||
       (manifest->rollback_size > OTA_APP_SIZE) ||
       (manifest->rollback_size > OTA_EXT_BACKUP_SIZE))
    {
        return 0U;
    }

    if(!ota_verify_ext_image(manifest->rollback_address, manifest->rollback_size, manifest->rollback_crc32))
    {
        return 0U;
    }

    manifest->boot_state = OTA_BOOT_STATE_ROLLING_BACK;
    (void)ota_manifest_store_both(manifest);

    if(HAL_FLASH_Unlock() != HAL_OK)
    {
        return 0U;
    }

    if(!ota_flash_erase_app(manifest->rollback_size) ||
       !ota_flash_program_app_from_ext(manifest->rollback_address, manifest->rollback_size))
    {
        (void)HAL_FLASH_Lock();
        return 0U;
    }

    (void)HAL_FLASH_Lock();

    if(ota_crc32_internal_flash(OTA_APP_BASE, manifest->rollback_size) != manifest->rollback_crc32)
    {
        return 0U;
    }

    manifest->boot_state = OTA_BOOT_STATE_NORMAL;
    manifest->image_size = manifest->rollback_size;
    manifest->image_crc32 = manifest->rollback_crc32;
    manifest->package_address = manifest->rollback_address;
    manifest->package_size = manifest->rollback_size;
    manifest->image_flags = 0U;
    return ota_manifest_store_both(manifest);
}

ota_boot_result_t ota_boot_process_update(void)
{
    ota_manifest_t manifest;

    if(ota_boot_v2_record_available())
    {
        return ota_boot_v2_process();
    }

    if(!ota_manifest_load_active(&manifest))
    {
        return OTA_BOOT_RESULT_NO_UPDATE;
    }

    if((manifest.boot_state == OTA_BOOT_STATE_NORMAL ||
        manifest.boot_state == OTA_BOOT_STATE_CONFIRMED) &&
       ota_boot_v2_migrate_confirmed_v1(&manifest))
    {
        return ota_boot_v2_process();
    }

    if(manifest.boot_state == OTA_BOOT_STATE_TRIAL_BOOT &&
       ota_boot_v2_migrate_trial_v1(&manifest))
    {
        return ota_boot_v2_process();
    }

    if(ota_manifest_is_rollback_state(manifest.boot_state))
    {
        if(ota_app_vector_is_valid())
        {
            return OTA_BOOT_RESULT_NO_UPDATE;
        }

        return ota_restore_backup_app(&manifest) ?
               OTA_BOOT_RESULT_ROLLED_BACK :
               OTA_BOOT_RESULT_ROLLBACK_FAILED;
    }

    if(!ota_manifest_is_update_state(manifest.boot_state))
    {
        return OTA_BOOT_RESULT_NO_UPDATE;
    }

    if((manifest.image_flags & OTA_IMAGE_FLAG_ENCRYPTED) != 0U)
    {
        return OTA_BOOT_RESULT_UNSUPPORTED;
    }

    if(!ota_manifest_image_bounds_ok(&manifest))
    {
        return OTA_BOOT_RESULT_BAD_MANIFEST;
    }

    if(!ota_verify_download_image(&manifest))
    {
        manifest.boot_state = OTA_BOOT_STATE_ROLLBACK_REQUIRED;
        (void)ota_manifest_store_both(&manifest);
        return OTA_BOOT_RESULT_BAD_IMAGE;
    }

    if(ota_app_vector_is_valid() &&
       ota_crc32_internal_flash(OTA_APP_BASE, manifest.image_size) == manifest.image_crc32)
    {
        manifest.boot_state = OTA_BOOT_STATE_TRIAL_BOOT;
        (void)ota_manifest_store_both(&manifest);
        return OTA_BOOT_RESULT_INSTALLED;
    }

    manifest.boot_state = OTA_BOOT_STATE_INSTALLING;
    if(!ota_backup_current_app(&manifest))
    {
        manifest.boot_state = OTA_BOOT_STATE_ROLLBACK_REQUIRED;
        (void)ota_manifest_store_both(&manifest);
        return OTA_BOOT_RESULT_FLASH_ERROR;
    }

    (void)ota_manifest_store_both(&manifest);

    if(HAL_FLASH_Unlock() != HAL_OK)
    {
        return OTA_BOOT_RESULT_FLASH_ERROR;
    }

    if(!ota_flash_erase_app(manifest.image_size) ||
       !ota_flash_program_app(&manifest) ||
       !ota_flash_verify_app(&manifest))
    {
        (void)HAL_FLASH_Lock();
        manifest.boot_state = OTA_BOOT_STATE_ROLLBACK_REQUIRED;
        (void)ota_manifest_store_both(&manifest);
        (void)ota_restore_backup_app(&manifest);
        return OTA_BOOT_RESULT_FLASH_ERROR;
    }

    (void)HAL_FLASH_Lock();

    manifest.boot_state = OTA_BOOT_STATE_TRIAL_BOOT;
    if(!ota_manifest_store_both(&manifest))
    {
        return OTA_BOOT_RESULT_FLASH_ERROR;
    }

    (void)ota_boot_v2_migrate_trial_v1(&manifest);
    return OTA_BOOT_RESULT_INSTALLED;
}
