#include "boot_control.h"

#include "boot_security.h"
#include "boot_spi_flash.h"
#include "boot_uart.h"
#include "bsp_qspi_w25q128.h"
#include "gateway_ota_format.h"
#include "stm32h7xx_hal.h"

#include <stddef.h>
#include <string.h>

#define BOOT_CONTROL_MAGIC            0x43423748UL
#define BOOT_CONTROL_SCHEMA           2UL
#define BOOT_CONTROL_COMMIT           0x00000000UL
#define BOOT_CONTROL_ERASED           0xFFFFFFFFUL
#define BOOT_IMAGE_MAGIC              0x50374148UL
#define BOOT_SLOT_NONE                0xFFFFFFFFUL
#define BOOT_SLOT_STATE_EMPTY         0UL
#define BOOT_SLOT_STATE_STAGED        1UL
#define BOOT_SLOT_STATE_TRIAL         2UL
#define BOOT_SLOT_STATE_CONFIRMED     3UL

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
} boot_image_descriptor_t;

typedef struct
{
    uint32_t magic;
    uint32_t schema;
    uint32_t sequence;
    uint32_t active_slot;
    uint32_t pending_slot;
    uint32_t minimum_version;
    uint32_t last_error;
    uint32_t reserved;
    boot_image_descriptor_t slots[2];
    uint32_t record_crc32;
    uint32_t commit_marker;
} boot_control_record_t;

static uint32_t boot_crc32_update(uint32_t crc,
                                  const uint8_t *data,
                                  uint32_t length);
static uint8_t boot_crc32_qspi(uint32_t address,
                               uint32_t size,
                               uint32_t *crc);
static uint8_t boot_crc32_spi(uint32_t address,
                              uint32_t size,
                              uint32_t *crc);
static uint8_t boot_control_record_valid(const boot_control_record_t *record);
static uint8_t boot_control_load(boot_control_record_t *record,
                                 uint32_t *record_address);
static uint8_t boot_control_store(const boot_control_record_t *record,
                                  uint32_t current_address,
                                  uint32_t *new_address);
static uint8_t boot_copy_qspi_region(uint32_t destination,
                                     uint32_t source,
                                     uint32_t size);
static uint8_t boot_copy_spi_to_qspi(uint32_t destination,
                                     uint32_t source,
                                     uint32_t size);
static uint8_t boot_factory_request(uint32_t *image_size,
                                    uint32_t *image_version);
static uint8_t boot_factory_enroll(boot_control_record_t *record,
                                   uint32_t *record_address);
static uint8_t boot_manifest_read_requested(
    gateway_ota_manifest_t *manifest,
    uint8_t *requested);
static uint8_t boot_manifest_valid(const gateway_ota_manifest_t *manifest);
static uint8_t boot_process_requested_package(boot_control_record_t *record,
                                              uint32_t *record_address);
static uint8_t boot_process_trial(boot_control_record_t *record,
                                  uint32_t *record_address,
                                  uint8_t *handled);
static uint8_t boot_start_staged_trial(boot_control_record_t *record,
                                       uint32_t *record_address,
                                       uint8_t *started);
static uint8_t boot_restore_active_image(const boot_control_record_t *record);
static uint8_t boot_descriptor_valid(const boot_image_descriptor_t *descriptor,
                                     uint32_t slot,
                                     uint32_t minimum_version,
                                     uint8_t signed_required,
                                     uint32_t *error);
static uint8_t boot_descriptor_signature_valid(
    const boot_image_descriptor_t *descriptor);
static void boot_descriptor_from_manifest(
    boot_image_descriptor_t *descriptor,
    const gateway_ota_manifest_t *manifest,
    uint32_t state);
static uint8_t boot_stage_mark_consumed(void);
static void boot_record_set_error(boot_control_record_t *record,
                                  uint32_t error);
static void boot_record_publish(const boot_control_record_t *record);
static uint32_t boot_slot_address(uint32_t slot);

boot_control_status_t boot_control_prepare(void)
{
    boot_control_record_t record;
    uint32_t record_address = 0U;
    uint32_t error = GATEWAY_OTA_BOOT_ERROR_NONE;
    uint8_t trial_handled = 0U;
    uint8_t trial_started = 0U;

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_CLK_ENABLE();

    if(boot_control_load(&record, &record_address) == 0U)
    {
        if(boot_factory_enroll(&record, &record_address) == 0U)
        {
            return (RTC->BKP0R == BOOT_IMAGE_MAGIC) ?
                   BOOT_CONTROL_ERROR : BOOT_CONTROL_NO_RECORD;
        }
        boot_uart_write("ART-Pi H750 boot: factory image enrolled\r\n");
    }

    if(record.active_slot > 1U ||
       record.slots[record.active_slot].state != BOOT_SLOT_STATE_CONFIRMED ||
       boot_descriptor_valid(&record.slots[record.active_slot],
                             record.active_slot,
                             record.minimum_version,
                             0U,
                             &error) == 0U)
    {
        boot_record_set_error(&record, error);
        boot_record_publish(&record);
        return BOOT_CONTROL_ERROR;
    }

    if(boot_process_trial(&record,
                          &record_address,
                          &trial_handled) == 0U)
    {
        boot_record_publish(&record);
        return BOOT_CONTROL_ERROR;
    }

    if(record.pending_slot != BOOT_SLOT_NONE)
    {
        if(boot_start_staged_trial(&record,
                                   &record_address,
                                   &trial_started) == 0U)
        {
            boot_record_publish(&record);
            return BOOT_CONTROL_ERROR;
        }
        if(trial_started != 0U)
        {
            boot_record_publish(&record);
            return BOOT_CONTROL_OK;
        }
    }

    if(boot_spi_flash_init() == HAL_OK)
    {
        if(boot_process_requested_package(&record, &record_address) == 0U)
        {
            boot_record_publish(&record);
            return BOOT_CONTROL_ERROR;
        }
        if(record.pending_slot != BOOT_SLOT_NONE)
        {
            if(boot_start_staged_trial(&record,
                                       &record_address,
                                       &trial_started) == 0U)
            {
                boot_record_publish(&record);
                return BOOT_CONTROL_ERROR;
            }
            if(trial_started != 0U)
            {
                boot_record_publish(&record);
                return BOOT_CONTROL_OK;
            }
        }
    }
    else
    {
        boot_uart_write("ART-Pi H750 boot: SPI staging unavailable, boot active image\r\n");
    }

    if(boot_restore_active_image(&record) == 0U)
    {
        boot_record_set_error(&record, GATEWAY_OTA_BOOT_ERROR_EXEC_WRITE);
        boot_record_publish(&record);
        return BOOT_CONTROL_ERROR;
    }

    (void)trial_handled;
    boot_record_publish(&record);
    return BOOT_CONTROL_OK;
}

static uint32_t boot_crc32_update(uint32_t crc,
                                  const uint8_t *data,
                                  uint32_t length)
{
    while(length-- != 0U)
    {
        uint32_t bit;
        crc ^= *data++;
        for(bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    }
    return crc;
}

static uint8_t boot_crc32_qspi(uint32_t address,
                               uint32_t size,
                               uint32_t *crc)
{
    uint8_t buffer[512];
    uint32_t value = 0xFFFFFFFFUL;

    if(crc == NULL)
    {
        return 0U;
    }
    while(size != 0U)
    {
        uint32_t length = (size > sizeof(buffer)) ? sizeof(buffer) : size;
        if(bsp_qspi_w25q128_read(address, buffer, length) != HAL_OK)
        {
            return 0U;
        }
        value = boot_crc32_update(value, buffer, length);
        address += length;
        size -= length;
    }
    *crc = value ^ 0xFFFFFFFFUL;
    return 1U;
}

static uint8_t boot_crc32_spi(uint32_t address,
                              uint32_t size,
                              uint32_t *crc)
{
    uint8_t buffer[512];
    uint32_t value = 0xFFFFFFFFUL;

    if(crc == NULL)
    {
        return 0U;
    }
    while(size != 0U)
    {
        uint32_t length = (size > sizeof(buffer)) ? sizeof(buffer) : size;
        if(boot_spi_flash_read(address, buffer, length) != HAL_OK)
        {
            return 0U;
        }
        value = boot_crc32_update(value, buffer, length);
        address += length;
        size -= length;
    }
    *crc = value ^ 0xFFFFFFFFUL;
    return 1U;
}

static uint8_t boot_control_record_valid(const boot_control_record_t *record)
{
    uint32_t crc;

    if(record->magic != BOOT_CONTROL_MAGIC ||
       record->schema != BOOT_CONTROL_SCHEMA ||
       record->commit_marker != BOOT_CONTROL_COMMIT)
    {
        return 0U;
    }
    crc = boot_crc32_update(0xFFFFFFFFUL,
                            (const uint8_t *)record,
                            offsetof(boot_control_record_t, record_crc32)) ^
          0xFFFFFFFFUL;
    return (crc == record->record_crc32) ? 1U : 0U;
}

static uint8_t boot_control_load(boot_control_record_t *record,
                                 uint32_t *record_address)
{
    boot_control_record_t first;
    boot_control_record_t second;
    uint8_t first_valid;
    uint8_t second_valid;

    if(bsp_qspi_w25q128_read(BOOT_CONTROL_A_ADDRESS,
                             (uint8_t *)&first,
                             sizeof(first)) != HAL_OK ||
       bsp_qspi_w25q128_read(BOOT_CONTROL_B_ADDRESS,
                             (uint8_t *)&second,
                             sizeof(second)) != HAL_OK)
    {
        return 0U;
    }
    first_valid = boot_control_record_valid(&first);
    second_valid = boot_control_record_valid(&second);
    if(first_valid == 0U && second_valid == 0U)
    {
        return 0U;
    }
    if(second_valid != 0U &&
       (first_valid == 0U || second.sequence > first.sequence))
    {
        *record = second;
        *record_address = BOOT_CONTROL_B_ADDRESS;
    }
    else
    {
        *record = first;
        *record_address = BOOT_CONTROL_A_ADDRESS;
    }
    return 1U;
}

static uint8_t boot_control_store(const boot_control_record_t *record,
                                  uint32_t current_address,
                                  uint32_t *new_address)
{
    boot_control_record_t write_record = *record;
    boot_control_record_t verify;
    uint32_t destination = (current_address == BOOT_CONTROL_A_ADDRESS) ?
                           BOOT_CONTROL_B_ADDRESS : BOOT_CONTROL_A_ADDRESS;
    uint32_t commit = BOOT_CONTROL_COMMIT;

    write_record.record_crc32 =
        boot_crc32_update(0xFFFFFFFFUL,
                          (const uint8_t *)&write_record,
                          offsetof(boot_control_record_t, record_crc32)) ^
        0xFFFFFFFFUL;
    write_record.commit_marker = BOOT_CONTROL_ERASED;
    if(bsp_qspi_w25q128_erase_sector(destination) != HAL_OK ||
       bsp_qspi_w25q128_program(destination,
                                (const uint8_t *)&write_record,
                                offsetof(boot_control_record_t,
                                         commit_marker)) != HAL_OK ||
       bsp_qspi_w25q128_read(destination,
                             (uint8_t *)&verify,
                             sizeof(verify)) != HAL_OK ||
       memcmp(&write_record,
              &verify,
              offsetof(boot_control_record_t, commit_marker)) != 0 ||
       bsp_qspi_w25q128_program(
           destination + offsetof(boot_control_record_t, commit_marker),
           (const uint8_t *)&commit,
           sizeof(commit)) != HAL_OK)
    {
        return 0U;
    }
    if(new_address != NULL)
    {
        *new_address = destination;
    }
    return 1U;
}

static uint8_t boot_copy_qspi_region(uint32_t destination,
                                     uint32_t source,
                                     uint32_t size)
{
    uint8_t buffer[512];
    uint32_t offset;
    uint32_t erase_size =
        (size + BSP_QSPI_W25Q128_SECTOR_SIZE - 1U) &
        ~(BSP_QSPI_W25Q128_SECTOR_SIZE - 1U);

    for(offset = 0U; offset < erase_size;
        offset += BSP_QSPI_W25Q128_SECTOR_SIZE)
    {
        if(bsp_qspi_w25q128_erase_sector(destination + offset) != HAL_OK)
        {
            return 0U;
        }
    }
    for(offset = 0U; offset < size; offset += sizeof(buffer))
    {
        uint32_t length = size - offset;
        if(length > sizeof(buffer))
        {
            length = sizeof(buffer);
        }
        if(bsp_qspi_w25q128_read(source + offset, buffer, length) != HAL_OK ||
           bsp_qspi_w25q128_program(destination + offset,
                                    buffer,
                                    length) != HAL_OK)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t boot_copy_spi_to_qspi(uint32_t destination,
                                     uint32_t source,
                                     uint32_t size)
{
    uint8_t buffer[512];
    uint32_t offset;
    uint32_t erase_size =
        (size + BSP_QSPI_W25Q128_SECTOR_SIZE - 1U) &
        ~(BSP_QSPI_W25Q128_SECTOR_SIZE - 1U);

    for(offset = 0U; offset < erase_size;
        offset += BSP_QSPI_W25Q128_SECTOR_SIZE)
    {
        if(bsp_qspi_w25q128_erase_sector(destination + offset) != HAL_OK)
        {
            return 0U;
        }
    }
    for(offset = 0U; offset < size; offset += sizeof(buffer))
    {
        uint32_t length = size - offset;
        if(length > sizeof(buffer))
        {
            length = sizeof(buffer);
        }
        if(boot_spi_flash_read(source + offset, buffer, length) != HAL_OK ||
           bsp_qspi_w25q128_program(destination + offset,
                                    buffer,
                                    length) != HAL_OK)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t boot_factory_request(uint32_t *image_size,
                                    uint32_t *image_version)
{
    if(RTC->BKP0R != BOOT_IMAGE_MAGIC || RTC->BKP1R == 0U ||
       RTC->BKP1R > BOOT_EXEC_SIZE)
    {
        return 0U;
    }
    *image_size = RTC->BKP1R;
    *image_version = RTC->BKP2R;
    return 1U;
}

static uint8_t boot_factory_enroll(boot_control_record_t *record,
                                   uint32_t *record_address)
{
    boot_image_descriptor_t *descriptor;
    uint32_t image_size;
    uint32_t image_version;

    if(boot_factory_request(&image_size, &image_version) == 0U)
    {
        return 0U;
    }
    memset(record, 0, sizeof(*record));
    record->magic = BOOT_CONTROL_MAGIC;
    record->schema = BOOT_CONTROL_SCHEMA;
    record->sequence = 1U;
    record->active_slot = 0U;
    record->pending_slot = BOOT_SLOT_NONE;
    record->minimum_version = image_version;
    descriptor = &record->slots[0];
    descriptor->state = BOOT_SLOT_STATE_CONFIRMED;
    descriptor->image_version = image_version;
    descriptor->image_size = image_size;
    descriptor->load_address = GATEWAY_OTA_LOAD_ADDRESS;
    if(bsp_qspi_w25q128_read(4U,
                             (uint8_t *)&descriptor->entry_address,
                             sizeof(descriptor->entry_address)) != HAL_OK ||
       boot_crc32_qspi(BOOT_EXEC_ADDRESS,
                       image_size,
                       &descriptor->image_crc32) == 0U ||
       boot_security_hash_qspi(BOOT_EXEC_ADDRESS,
                               image_size,
                               descriptor->image_sha256) == 0U ||
       boot_copy_qspi_region(BOOT_SLOT_A_ADDRESS,
                             BOOT_EXEC_ADDRESS,
                             image_size) == 0U ||
       boot_control_store(record, 0U, record_address) == 0U)
    {
        return 0U;
    }
    RTC->BKP0R = 0U;
    return 1U;
}

static uint8_t boot_manifest_read_requested(
    gateway_ota_manifest_t *manifest,
    uint8_t *requested)
{
    uint32_t complete;
    uint32_t request;
    uint32_t consumed;

    *requested = 0U;
    if(boot_spi_flash_read(GATEWAY_OTA_STAGE_COMPLETE_ADDRESS,
                           (uint8_t *)&complete,
                           sizeof(complete)) != HAL_OK ||
       boot_spi_flash_read(GATEWAY_OTA_STAGE_REQUEST_ADDRESS,
                           (uint8_t *)&request,
                           sizeof(request)) != HAL_OK ||
       boot_spi_flash_read(GATEWAY_OTA_STAGE_CONSUMED_ADDRESS,
                           (uint8_t *)&consumed,
                           sizeof(consumed)) != HAL_OK)
    {
        return 0U;
    }
    if(complete != GATEWAY_OTA_MARKER_SET ||
       request != GATEWAY_OTA_MARKER_SET ||
       consumed == GATEWAY_OTA_MARKER_SET)
    {
        return 1U;
    }
    if(boot_spi_flash_read(GATEWAY_OTA_STAGE_ADDRESS,
                           (uint8_t *)manifest,
                           sizeof(*manifest)) != HAL_OK)
    {
        return 0U;
    }
    *requested = 1U;
    return 1U;
}

static uint8_t boot_manifest_valid(const gateway_ota_manifest_t *manifest)
{
    gateway_ota_manifest_t copy;
    uint32_t crc;

    copy = *manifest;
    copy.manifest_crc32 = 0U;
    crc = boot_crc32_update(0xFFFFFFFFUL,
                            (const uint8_t *)&copy,
                            sizeof(copy)) ^ 0xFFFFFFFFUL;
    if(manifest->magic != GATEWAY_OTA_MANIFEST_MAGIC ||
       manifest->version != GATEWAY_OTA_MANIFEST_SCHEMA ||
       manifest->manifest_crc32 != crc ||
       manifest->image_size == 0U ||
       manifest->image_size > BOOT_SLOT_SIZE ||
       manifest->package_address != GATEWAY_OTA_STAGE_IMAGE_ADDRESS ||
       manifest->package_size != manifest->image_size ||
       manifest->load_address != GATEWAY_OTA_LOAD_ADDRESS ||
       manifest->entry_address < GATEWAY_OTA_LOAD_ADDRESS ||
       manifest->entry_address >=
           (GATEWAY_OTA_LOAD_ADDRESS + BOOT_EXEC_SIZE) ||
       (manifest->entry_address & 1U) == 0U ||
       (manifest->image_flags & GATEWAY_OTA_IMAGE_FLAG_SIGNED) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static uint8_t boot_process_requested_package(boot_control_record_t *record,
                                              uint32_t *record_address)
{
    gateway_ota_manifest_t manifest;
    boot_image_descriptor_t descriptor;
    uint8_t requested;
    uint8_t digest[32];
    uint32_t crc;
    uint32_t inactive_slot;
    uint32_t inactive_address;

    if(boot_manifest_read_requested(&manifest, &requested) == 0U)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_STAGE_IO);
        return 1U;
    }
    if(requested == 0U)
    {
        return 1U;
    }
    boot_uart_write("ART-Pi H750 boot: signed gateway update requested\r\n");

    if(boot_manifest_valid(&manifest) == 0U)
    {
        boot_uart_write("ART-Pi H750 boot: update manifest rejected\r\n");
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_MANIFEST);
        (void)boot_stage_mark_consumed();
        return 1U;
    }
    if(manifest.image_version <= record->minimum_version)
    {
        boot_uart_write("ART-Pi H750 boot: update version rejected\r\n");
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_VERSION);
        (void)boot_stage_mark_consumed();
        return 1U;
    }
    if(boot_crc32_spi(GATEWAY_OTA_STAGE_IMAGE_ADDRESS,
                      manifest.image_size,
                      &crc) == 0U)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_STAGE_IO);
        return 1U;
    }
    if(crc != manifest.image_crc32)
    {
        boot_uart_write("ART-Pi H750 boot: update CRC rejected\r\n");
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_IMAGE_CRC);
        (void)boot_stage_mark_consumed();
        return 1U;
    }
    if(boot_security_hash_spi(GATEWAY_OTA_STAGE_IMAGE_ADDRESS,
                              manifest.image_size,
                              digest) == 0U)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_STAGE_IO);
        return 1U;
    }
    if(memcmp(digest, manifest.image_sha256, sizeof(digest)) != 0 ||
       memcmp(digest, manifest.package_sha256, sizeof(digest)) != 0)
    {
        boot_uart_write("ART-Pi H750 boot: update SHA256 rejected\r\n");
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_IMAGE_SHA256);
        (void)boot_stage_mark_consumed();
        return 1U;
    }
    if(boot_security_verify_manifest_signature(&manifest) == 0U)
    {
        boot_uart_write("ART-Pi H750 boot: update signature rejected\r\n");
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_SIGNATURE);
        (void)boot_stage_mark_consumed();
        return 1U;
    }

    inactive_slot = (record->active_slot == 0U) ? 1U : 0U;
    inactive_address = boot_slot_address(inactive_slot);
    if(boot_copy_spi_to_qspi(inactive_address,
                             GATEWAY_OTA_STAGE_IMAGE_ADDRESS,
                             manifest.image_size) == 0U)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_SLOT_WRITE);
        return 1U;
    }
    boot_descriptor_from_manifest(&descriptor,
                                  &manifest,
                                  BOOT_SLOT_STATE_STAGED);
    if(boot_descriptor_valid(&descriptor,
                             inactive_slot,
                             record->minimum_version + 1U,
                             1U,
                             &crc) == 0U)
    {
        boot_record_set_error(record, crc);
        return 1U;
    }

    record->slots[inactive_slot] = descriptor;
    record->pending_slot = inactive_slot;
    record->last_error = GATEWAY_OTA_BOOT_ERROR_NONE;
    record->sequence++;
    if(boot_control_store(record,
                          *record_address,
                          record_address) == 0U)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_CONTROL);
        return 0U;
    }
    if(boot_stage_mark_consumed() == 0U)
    {
        boot_uart_write("ART-Pi H750 boot: update staged; consume marker retry later\r\n");
    }
    boot_uart_write("ART-Pi H750 boot: update stored in inactive slot\r\n");
    return 1U;
}

static uint8_t boot_process_trial(boot_control_record_t *record,
                                  uint32_t *record_address,
                                  uint8_t *handled)
{
    uint32_t pending;

    *handled = 0U;
    if(record->pending_slot == BOOT_SLOT_NONE)
    {
        return 1U;
    }
    pending = record->pending_slot;
    if(pending > 1U || record->slots[pending].state != BOOT_SLOT_STATE_TRIAL)
    {
        return 1U;
    }
    *handled = 1U;
    if(RTC->BKP3R == GATEWAY_OTA_HEALTH_MAGIC &&
       RTC->BKP6R == GATEWAY_OTA_TRIAL_MAGIC)
    {
        record->slots[pending].state = BOOT_SLOT_STATE_CONFIRMED;
        record->active_slot = pending;
        record->pending_slot = BOOT_SLOT_NONE;
        record->minimum_version = record->slots[pending].image_version;
        record->last_error = GATEWAY_OTA_BOOT_ERROR_NONE;
        record->sequence++;
        if(boot_control_store(record,
                              *record_address,
                              record_address) == 0U)
        {
            return 0U;
        }
        RTC->BKP3R = 0U;
        RTC->BKP6R = 0U;
        boot_uart_write("ART-Pi H750 boot: trial image confirmed\r\n");
        return 1U;
    }

    boot_uart_write("ART-Pi H750 boot: trial health missing, rollback\r\n");
    if(boot_copy_qspi_region(BOOT_EXEC_ADDRESS,
                             boot_slot_address(record->active_slot),
                             record->slots[record->active_slot].image_size) == 0U)
    {
        boot_record_set_error(record,
                              GATEWAY_OTA_BOOT_ERROR_TRIAL_ROLLBACK);
        return 0U;
    }
    memset(&record->slots[pending], 0, sizeof(record->slots[pending]));
    record->pending_slot = BOOT_SLOT_NONE;
    record->last_error = GATEWAY_OTA_BOOT_ERROR_TRIAL_ROLLBACK;
    record->sequence++;
    if(boot_control_store(record,
                          *record_address,
                          record_address) == 0U)
    {
        return 0U;
    }
    RTC->BKP3R = 0U;
    RTC->BKP6R = 0U;
    return 1U;
}

static uint8_t boot_start_staged_trial(boot_control_record_t *record,
                                       uint32_t *record_address,
                                       uint8_t *started)
{
    uint32_t pending;
    uint32_t error;
    uint32_t crc;
    uint8_t digest[32];

    *started = 0U;
    if(record->pending_slot == BOOT_SLOT_NONE)
    {
        return 1U;
    }
    pending = record->pending_slot;
    if(pending > 1U || record->slots[pending].state != BOOT_SLOT_STATE_STAGED)
    {
        return 1U;
    }
    if(boot_descriptor_valid(&record->slots[pending],
                             pending,
                             record->minimum_version + 1U,
                             1U,
                             &error) == 0U)
    {
        boot_record_set_error(record, error);
        return 0U;
    }
    if(boot_copy_qspi_region(BOOT_EXEC_ADDRESS,
                             boot_slot_address(pending),
                             record->slots[pending].image_size) == 0U ||
       boot_crc32_qspi(BOOT_EXEC_ADDRESS,
                       record->slots[pending].image_size,
                       &crc) == 0U ||
       crc != record->slots[pending].image_crc32 ||
       boot_security_hash_qspi(BOOT_EXEC_ADDRESS,
                               record->slots[pending].image_size,
                               digest) == 0U ||
       memcmp(digest,
              record->slots[pending].image_sha256,
              sizeof(digest)) != 0)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_EXEC_WRITE);
        return 0U;
    }

    record->slots[pending].state = BOOT_SLOT_STATE_TRIAL;
    record->last_error = GATEWAY_OTA_BOOT_ERROR_NONE;
    record->sequence++;
    if(boot_control_store(record,
                          *record_address,
                          record_address) == 0U)
    {
        boot_record_set_error(record, GATEWAY_OTA_BOOT_ERROR_CONTROL);
        return 0U;
    }
    RTC->BKP3R = 0U;
    RTC->BKP6R = GATEWAY_OTA_TRIAL_MAGIC;
    *started = 1U;
    boot_uart_write("ART-Pi H750 boot: trial image installed\r\n");
    return 1U;
}

static uint8_t boot_restore_active_image(const boot_control_record_t *record)
{
    const boot_image_descriptor_t *descriptor =
        &record->slots[record->active_slot];
    uint32_t crc;

    if(boot_crc32_qspi(BOOT_EXEC_ADDRESS,
                       descriptor->image_size,
                       &crc) == 0U)
    {
        return 0U;
    }
    if(crc == descriptor->image_crc32)
    {
        return 1U;
    }
    boot_uart_write("ART-Pi H750 boot: restoring active QSPI image\r\n");
    if(boot_copy_qspi_region(BOOT_EXEC_ADDRESS,
                             boot_slot_address(record->active_slot),
                             descriptor->image_size) == 0U ||
       boot_crc32_qspi(BOOT_EXEC_ADDRESS,
                       descriptor->image_size,
                       &crc) == 0U ||
       crc != descriptor->image_crc32)
    {
        return 0U;
    }
    return 1U;
}

static uint8_t boot_descriptor_valid(const boot_image_descriptor_t *descriptor,
                                     uint32_t slot,
                                     uint32_t minimum_version,
                                     uint8_t signed_required,
                                     uint32_t *error)
{
    uint8_t digest[32];
    uint32_t crc;

    *error = GATEWAY_OTA_BOOT_ERROR_MANIFEST;
    if(slot > 1U || descriptor->image_size == 0U ||
       descriptor->image_size > BOOT_SLOT_SIZE ||
       descriptor->load_address != GATEWAY_OTA_LOAD_ADDRESS ||
       descriptor->entry_address < GATEWAY_OTA_LOAD_ADDRESS ||
       descriptor->entry_address >=
           (GATEWAY_OTA_LOAD_ADDRESS + BOOT_EXEC_SIZE) ||
       (descriptor->entry_address & 1U) == 0U)
    {
        return 0U;
    }
    if(descriptor->image_version < minimum_version)
    {
        *error = GATEWAY_OTA_BOOT_ERROR_VERSION;
        return 0U;
    }
    if(boot_crc32_qspi(boot_slot_address(slot),
                       descriptor->image_size,
                       &crc) == 0U ||
       crc != descriptor->image_crc32)
    {
        *error = GATEWAY_OTA_BOOT_ERROR_IMAGE_CRC;
        return 0U;
    }
    if(boot_security_hash_qspi(boot_slot_address(slot),
                               descriptor->image_size,
                               digest) == 0U ||
       memcmp(digest, descriptor->image_sha256, sizeof(digest)) != 0)
    {
        *error = GATEWAY_OTA_BOOT_ERROR_IMAGE_SHA256;
        return 0U;
    }
    if((descriptor->image_flags & GATEWAY_OTA_IMAGE_FLAG_SIGNED) == 0U)
    {
        if(signed_required != 0U)
        {
            *error = GATEWAY_OTA_BOOT_ERROR_SIGNATURE;
            return 0U;
        }
    }
    else if(boot_descriptor_signature_valid(descriptor) == 0U)
    {
        *error = GATEWAY_OTA_BOOT_ERROR_SIGNATURE;
        return 0U;
    }
    *error = GATEWAY_OTA_BOOT_ERROR_NONE;
    return 1U;
}

static uint8_t boot_descriptor_signature_valid(
    const boot_image_descriptor_t *descriptor)
{
    gateway_ota_manifest_t manifest;

    memset(&manifest, 0, sizeof(manifest));
    manifest.image_version = descriptor->image_version;
    manifest.image_size = descriptor->image_size;
    manifest.image_crc32 = descriptor->image_crc32;
    manifest.image_flags = descriptor->image_flags;
    manifest.load_address = descriptor->load_address;
    manifest.entry_address = descriptor->entry_address;
    memcpy(manifest.image_sha256, descriptor->image_sha256, 32U);
    memcpy(manifest.signature, descriptor->signature, 64U);
    return boot_security_verify_manifest_signature(&manifest);
}

static void boot_descriptor_from_manifest(
    boot_image_descriptor_t *descriptor,
    const gateway_ota_manifest_t *manifest,
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
    memcpy(descriptor->image_sha256, manifest->image_sha256, 32U);
    memcpy(descriptor->signature, manifest->signature, 64U);
}

static uint8_t boot_stage_mark_consumed(void)
{
    uint32_t marker = GATEWAY_OTA_MARKER_SET;
    uint32_t verify = GATEWAY_OTA_MARKER_ERASED;

    if(boot_spi_flash_program(GATEWAY_OTA_STAGE_CONSUMED_ADDRESS,
                              (const uint8_t *)&marker,
                              sizeof(marker)) != HAL_OK ||
       boot_spi_flash_read(GATEWAY_OTA_STAGE_CONSUMED_ADDRESS,
                           (uint8_t *)&verify,
                           sizeof(verify)) != HAL_OK)
    {
        return 0U;
    }
    return (verify == GATEWAY_OTA_MARKER_SET) ? 1U : 0U;
}

static void boot_record_set_error(boot_control_record_t *record,
                                  uint32_t error)
{
    record->last_error = error;
    RTC->BKP7R = error;
}

static void boot_record_publish(const boot_control_record_t *record)
{
    RTC->BKP7R = record->last_error;
    RTC->BKP8R = (record->active_slot <= 1U) ?
                 record->slots[record->active_slot].image_version : 0U;
    RTC->BKP9R = (record->pending_slot <= 1U) ?
                 record->slots[record->pending_slot].image_version : 0U;
    RTC->BKP10R = record->sequence;
}

static uint32_t boot_slot_address(uint32_t slot)
{
    return (slot == 0U) ? BOOT_SLOT_A_ADDRESS : BOOT_SLOT_B_ADDRESS;
}
