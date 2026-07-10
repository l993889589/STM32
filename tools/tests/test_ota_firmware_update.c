/*
 * Host tests for inactive-slot firmware transactions.
 * Tests prove that incomplete and corrupt downloads never modify active slot A.
 */
#include "ota_firmware_update.h"
#include "ota_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_FLASH_SIZE OTA_EXT_DIAGNOSTIC_ADDR
#define TEST_IMAGE_SIZE 1024U

typedef struct
{
    uint8_t *bytes;
    uint8_t fail_writes;
} fake_firmware_flash_t;

static int test_failures;

#define TEST_CHECK(condition) \
    do { if(!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        test_failures++; \
    } } while(0)

static uint32_t test_crc32(uint32_t crc, const uint8_t *data, uint32_t size)
{
    crc = ~crc;
    while(size-- != 0U)
    {
        uint32_t bit;
        crc ^= *data++;
        for(bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                  ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return ~crc;
}

static uint8_t fake_read(void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    fake_firmware_flash_t *flash = (fake_firmware_flash_t *)context;
    if(address > TEST_FLASH_SIZE || size > TEST_FLASH_SIZE - address)
    {
        return 0U;
    }
    memcpy(data, &flash->bytes[address], size);
    return 1U;
}

static uint8_t fake_erase(void *context, uint32_t address)
{
    fake_firmware_flash_t *flash = (fake_firmware_flash_t *)context;
    if((address % OTA_EXT_SECTOR_SIZE) != 0U ||
       address > TEST_FLASH_SIZE - OTA_EXT_SECTOR_SIZE)
    {
        return 0U;
    }
    memset(&flash->bytes[address], 0xFF, OTA_EXT_SECTOR_SIZE);
    return 1U;
}

static uint8_t fake_write(void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    fake_firmware_flash_t *flash = (fake_firmware_flash_t *)context;
    uint32_t index;

    if(flash->fail_writes != 0U ||
       address > TEST_FLASH_SIZE || size > TEST_FLASH_SIZE - address)
    {
        return 0U;
    }

    for(index = 0U; index < size; index++)
    {
        flash->bytes[address + index] &= data[index];
    }
    return 1U;
}

static ota_firmware_update_storage_t fake_storage(fake_firmware_flash_t *flash)
{
    ota_firmware_update_storage_t storage;
    storage.context = flash;
    storage.read = fake_read;
    storage.erase_sector = fake_erase;
    storage.write = fake_write;
    return storage;
}

static void make_descriptor(
    ota_firmware_descriptor_t *descriptor,
    const uint8_t *image,
    uint32_t image_size)
{
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->state = OTA_SLOT_STATE_VERIFIED;
    descriptor->image_version = 2026071001U;
    descriptor->image_size = image_size;
    descriptor->image_crc32 = test_crc32(0U, image, image_size);
    ota_sha256_calculate(image, image_size, descriptor->image_sha256);
    descriptor->load_address = OTA_APP_BASE;
    descriptor->entry_address = OTA_APP_BASE + 0x101U;
}

static void provision_active_slot_a(fake_firmware_flash_t *flash)
{
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t record;
    ota_boot_control_record_t committed;
    ota_control_copy_t copy;

    storage.context = flash;
    storage.read = fake_read;
    storage.erase_sector = fake_erase;
    storage.write = fake_write;

    ota_boot_control_init(&record);
    record.state = OTA_CONTROL_STATE_CONFIRMED;
    record.active_slot = OTA_FIRMWARE_SLOT_A;
    record.slots[OTA_FIRMWARE_SLOT_A].state = OTA_SLOT_STATE_CONFIRMED;
    record.slots[OTA_FIRMWARE_SLOT_A].image_version = 2026070901U;
    record.slots[OTA_FIRMWARE_SLOT_A].image_size = TEST_IMAGE_SIZE;
    record.slots[OTA_FIRMWARE_SLOT_A].image_crc32 = 0x12345678U;
    record.slots[OTA_FIRMWARE_SLOT_A].load_address = OTA_APP_BASE;
    record.slots[OTA_FIRMWARE_SLOT_A].entry_address = OTA_APP_BASE + 0x101U;
    record.minimum_version = record.slots[OTA_FIRMWARE_SLOT_A].image_version;

    TEST_CHECK(ota_boot_control_storage_store(&storage, &record, &committed, &copy) ==
               OTA_CONTROL_STATUS_OK);
    memset(&flash->bytes[OTA_EXT_FIRMWARE_SLOT_A_ADDR], 0xA5, TEST_IMAGE_SIZE);
}

static void test_sha256_known_vector(void)
{
    static const uint8_t expected[OTA_SHA256_DIGEST_SIZE] =
    {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    uint8_t actual[OTA_SHA256_DIGEST_SIZE];

    ota_sha256_calculate((const uint8_t *)"abc", 3U, actual);
    TEST_CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
}

static void test_incomplete_download_preserves_active_slot(void)
{
    fake_firmware_flash_t flash;
    ota_firmware_update_storage_t storage;
    ota_firmware_update_t update;
    ota_firmware_descriptor_t descriptor;
    uint8_t image[TEST_IMAGE_SIZE];
    uint32_t index;

    flash.bytes = (uint8_t *)malloc(TEST_FLASH_SIZE);
    TEST_CHECK(flash.bytes != NULL);
    if(flash.bytes == NULL)
    {
        return;
    }
    memset(flash.bytes, 0xFF, TEST_FLASH_SIZE);
    flash.fail_writes = 0U;
    provision_active_slot_a(&flash);

    for(index = 0U; index < sizeof(image); index++)
    {
        image[index] = (uint8_t)(index ^ 0x5AU);
    }
    make_descriptor(&descriptor, image, sizeof(image));
    storage = fake_storage(&flash);

    TEST_CHECK(ota_firmware_update_init(&update, &storage) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_begin(&update, &descriptor) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(update.target_slot == OTA_FIRMWARE_SLOT_B);
    TEST_CHECK(ota_firmware_update_write(&update, 0U, image, 400U) ==
               OTA_FIRMWARE_UPDATE_OK);

    for(index = 0U; index < TEST_IMAGE_SIZE; index++)
    {
        TEST_CHECK(flash.bytes[OTA_EXT_FIRMWARE_SLOT_A_ADDR + index] == 0xA5U);
    }

    free(flash.bytes);
}

static void test_rollback_version_rejected_before_erase(void)
{
    fake_firmware_flash_t flash;
    ota_firmware_update_storage_t storage;
    ota_boot_control_storage_t control_storage;
    ota_boot_control_record_t control;
    ota_firmware_update_t update;
    ota_firmware_descriptor_t descriptor;
    ota_control_copy_t copy;
    uint8_t image[TEST_IMAGE_SIZE];
    uint32_t index;

    flash.bytes = (uint8_t *)malloc(TEST_FLASH_SIZE);
    TEST_CHECK(flash.bytes != NULL);
    if(flash.bytes == NULL)
    {
        return;
    }
    memset(flash.bytes, 0xFF, TEST_FLASH_SIZE);
    flash.fail_writes = 0U;
    memset(image, 0xA6, sizeof(image));
    provision_active_slot_a(&flash);
    make_descriptor(&descriptor, image, sizeof(image));
    descriptor.image_version = 2026070900U;
    storage = fake_storage(&flash);

    TEST_CHECK(ota_firmware_update_init(&update, &storage) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_begin(&update, &descriptor) ==
               OTA_FIRMWARE_UPDATE_VERSION_ROLLBACK);
    TEST_CHECK(update.is_active == 0U);

    for(index = 0U; index < TEST_IMAGE_SIZE; index++)
    {
        TEST_CHECK(flash.bytes[OTA_EXT_FIRMWARE_SLOT_B_ADDR + index] == 0xFFU);
    }

    control_storage.context = &flash;
    control_storage.read = fake_read;
    control_storage.erase_sector = fake_erase;
    control_storage.write = fake_write;
    TEST_CHECK(ota_boot_control_storage_load(&control_storage, &control, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(control.state == OTA_CONTROL_STATE_CONFIRMED);
    TEST_CHECK(control.pending_slot == OTA_FIRMWARE_SLOT_NONE);

    free(flash.bytes);
}

static void test_complete_download_becomes_pending(void)
{
    fake_firmware_flash_t flash;
    ota_firmware_update_storage_t storage;
    ota_boot_control_storage_t control_storage;
    ota_boot_control_record_t control;
    ota_firmware_update_t update;
    ota_firmware_descriptor_t descriptor;
    ota_control_copy_t copy;
    uint8_t image[TEST_IMAGE_SIZE];
    uint32_t index;

    flash.bytes = (uint8_t *)malloc(TEST_FLASH_SIZE);
    TEST_CHECK(flash.bytes != NULL);
    if(flash.bytes == NULL)
    {
        return;
    }
    memset(flash.bytes, 0xFF, TEST_FLASH_SIZE);
    flash.fail_writes = 0U;
    provision_active_slot_a(&flash);

    for(index = 0U; index < sizeof(image); index++)
    {
        image[index] = (uint8_t)(index * 17U + 3U);
    }
    make_descriptor(&descriptor, image, sizeof(image));
    storage = fake_storage(&flash);

    TEST_CHECK(ota_firmware_update_init(&update, &storage) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_begin(&update, &descriptor) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_write(&update, 0U, image, 512U) ==
               OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_write(&update, 512U, &image[512], 512U) ==
               OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_finish(&update) == OTA_FIRMWARE_UPDATE_OK);

    control_storage.context = &flash;
    control_storage.read = fake_read;
    control_storage.erase_sector = fake_erase;
    control_storage.write = fake_write;
    TEST_CHECK(ota_boot_control_storage_load(&control_storage, &control, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(control.state == OTA_CONTROL_STATE_PENDING);
    TEST_CHECK(control.active_slot == OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(control.pending_slot == OTA_FIRMWARE_SLOT_B);
    TEST_CHECK(control.slots[OTA_FIRMWARE_SLOT_B].state == OTA_SLOT_STATE_VERIFIED);

    for(index = 0U; index < TEST_IMAGE_SIZE; index++)
    {
        TEST_CHECK(flash.bytes[OTA_EXT_FIRMWARE_SLOT_A_ADDR + index] == 0xA5U);
    }

    free(flash.bytes);
}

static void test_bad_crc_never_becomes_pending(void)
{
    fake_firmware_flash_t flash;
    ota_firmware_update_storage_t storage;
    ota_boot_control_storage_t control_storage;
    ota_boot_control_record_t control;
    ota_firmware_update_t update;
    ota_firmware_descriptor_t descriptor;
    ota_control_copy_t copy;
    uint8_t image[TEST_IMAGE_SIZE];

    flash.bytes = (uint8_t *)malloc(TEST_FLASH_SIZE);
    TEST_CHECK(flash.bytes != NULL);
    if(flash.bytes == NULL)
    {
        return;
    }
    memset(flash.bytes, 0xFF, TEST_FLASH_SIZE);
    flash.fail_writes = 0U;
    memset(image, 0x3C, sizeof(image));
    provision_active_slot_a(&flash);
    make_descriptor(&descriptor, image, sizeof(image));
    descriptor.image_crc32 ^= 1U;
    storage = fake_storage(&flash);

    TEST_CHECK(ota_firmware_update_init(&update, &storage) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_begin(&update, &descriptor) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_write(&update, 0U, image, sizeof(image)) ==
               OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_finish(&update) == OTA_FIRMWARE_UPDATE_CRC_MISMATCH);

    control_storage.context = &flash;
    control_storage.read = fake_read;
    control_storage.erase_sector = fake_erase;
    control_storage.write = fake_write;
    TEST_CHECK(ota_boot_control_storage_load(&control_storage, &control, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(control.state == OTA_CONTROL_STATE_DOWNLOADING);
    TEST_CHECK(control.active_slot == OTA_FIRMWARE_SLOT_A);

    free(flash.bytes);
}

static void test_recovery_provisions_first_slot(void)
{
    fake_firmware_flash_t flash;
    ota_firmware_update_storage_t storage;
    ota_boot_control_storage_t control_storage;
    ota_boot_control_record_t control;
    ota_firmware_update_t update;
    ota_firmware_descriptor_t descriptor;
    ota_control_copy_t copy;
    uint8_t image[TEST_IMAGE_SIZE];

    flash.bytes = (uint8_t *)malloc(TEST_FLASH_SIZE);
    TEST_CHECK(flash.bytes != NULL);
    if(flash.bytes == NULL)
    {
        return;
    }
    memset(flash.bytes, 0xFF, TEST_FLASH_SIZE);
    flash.fail_writes = 0U;
    memset(image, 0x69, sizeof(image));
    make_descriptor(&descriptor, image, sizeof(image));
    storage = fake_storage(&flash);

    TEST_CHECK(ota_firmware_update_init(&update, &storage) == OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_begin(&update, &descriptor) ==
               OTA_FIRMWARE_UPDATE_NOT_PROVISIONED);
    TEST_CHECK(ota_firmware_update_begin_recovery(&update, &descriptor) ==
               OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(update.target_slot == OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(ota_firmware_update_write(&update, 0U, image, sizeof(image)) ==
               OTA_FIRMWARE_UPDATE_OK);
    TEST_CHECK(ota_firmware_update_finish(&update) == OTA_FIRMWARE_UPDATE_OK);

    control_storage.context = &flash;
    control_storage.read = fake_read;
    control_storage.erase_sector = fake_erase;
    control_storage.write = fake_write;
    TEST_CHECK(ota_boot_control_storage_load(&control_storage, &control, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(control.state == OTA_CONTROL_STATE_PENDING);
    TEST_CHECK(control.active_slot == OTA_FIRMWARE_SLOT_NONE);
    TEST_CHECK(control.pending_slot == OTA_FIRMWARE_SLOT_A);

    free(flash.bytes);
}

int main(void)
{
    test_sha256_known_vector();
    test_incomplete_download_preserves_active_slot();
    test_complete_download_becomes_pending();
    test_bad_crc_never_becomes_pending();
    test_rollback_version_rejected_before_erase();
    test_recovery_provisions_first_slot();

    if(test_failures != 0)
    {
        printf("ota_firmware_update: %d failure(s)\n", test_failures);
        return 1;
    }

    printf("ota_firmware_update: all tests passed\n");
    return 0;
}
