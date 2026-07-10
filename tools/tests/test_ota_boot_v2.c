/*
 * Host tests for bootloader OTA v2 install, retry, trial and rollback policy.
 * Hardware flash operations are replaced with deterministic in-memory stubs.
 */
#include "ota_boot_v2.h"
#include "ota_boot_private.h"
#include "ota_boot_control.h"
#include "gd25lq128.h"
#include <stdio.h>
#include <string.h>

static uint8_t metadata[2U * OTA_EXT_SECTOR_SIZE];
static uint32_t internal_size;
static uint32_t internal_crc;
static uint8_t fail_install;
static uint8_t external_valid_a;
static uint8_t external_valid_b;
static uint8_t security_valid;
static int test_failures;

#define TEST_CHECK(condition) \
    do { if(!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        test_failures++; \
    } } while(0)

static void test_reset(void)
{
    memset(metadata, 0xFF, sizeof(metadata));
    internal_size = 0U;
    internal_crc = 0U;
    fail_install = 0U;
    external_valid_a = 1U;
    external_valid_b = 1U;
    security_valid = 1U;
}

uint8_t boot_security_verify_slot(
    const ota_boot_control_record_t *record,
    uint32_t slot,
    uint32_t *control_error)
{
    (void)record;
    (void)slot;
    if(control_error != NULL)
    {
        *control_error = (uint32_t)OTA_CONTROL_ERROR_IMAGE_SIGNATURE;
    }
    return security_valid;
}

uint32_t ota_boot_reset_reason(void)
{
    return 0x12345678U;
}

static uint8_t test_metadata_read(
    void *context,
    uint32_t address,
    uint8_t *data,
    uint32_t size)
{
    (void)context;
    if(address > sizeof(metadata) || size > sizeof(metadata) - address)
    {
        return 0U;
    }
    memcpy(data, &metadata[address], size);
    return 1U;
}

static uint8_t test_metadata_erase(void *context, uint32_t address)
{
    (void)context;
    if((address % OTA_EXT_SECTOR_SIZE) != 0U ||
       address > sizeof(metadata) - OTA_EXT_SECTOR_SIZE)
    {
        return 0U;
    }
    memset(&metadata[address], 0xFF, OTA_EXT_SECTOR_SIZE);
    return 1U;
}

static uint8_t test_metadata_write(
    void *context,
    uint32_t address,
    const uint8_t *data,
    uint32_t size)
{
    uint32_t index;
    (void)context;

    if(address > sizeof(metadata) || size > sizeof(metadata) - address)
    {
        return 0U;
    }
    for(index = 0U; index < size; index++)
    {
        metadata[address + index] &= data[index];
    }
    return 1U;
}

static ota_boot_control_storage_t test_control_storage(void)
{
    ota_boot_control_storage_t storage;
    storage.context = NULL;
    storage.read = test_metadata_read;
    storage.erase_sector = test_metadata_erase;
    storage.write = test_metadata_write;
    return storage;
}

static void test_descriptor(
    ota_firmware_descriptor_t *descriptor,
    uint32_t state,
    uint32_t version,
    uint32_t crc)
{
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->state = state;
    descriptor->image_version = version;
    descriptor->image_size = 1024U;
    descriptor->image_crc32 = crc;
    descriptor->load_address = OTA_APP_BASE;
    descriptor->entry_address = OTA_APP_BASE + 0x101U;
}

static void test_store_record(uint32_t state)
{
    ota_boot_control_storage_t storage = test_control_storage();
    ota_boot_control_record_t record;
    ota_boot_control_record_t committed;
    ota_control_copy_t copy;

    ota_boot_control_init(&record);
    record.state = state;
    record.active_slot = OTA_FIRMWARE_SLOT_A;
    test_descriptor(&record.slots[OTA_FIRMWARE_SLOT_A],
                    OTA_SLOT_STATE_CONFIRMED, 100U, 0xAAAA1111U);

    if(state != OTA_CONTROL_STATE_CONFIRMED)
    {
        record.pending_slot = OTA_FIRMWARE_SLOT_B;
        test_descriptor(&record.slots[OTA_FIRMWARE_SLOT_B],
                        OTA_SLOT_STATE_VERIFIED, 101U, 0xBBBB2222U);
    }

    TEST_CHECK(ota_boot_control_storage_store(&storage, &record, &committed, &copy) ==
               OTA_CONTROL_STATUS_OK);
}

static ota_boot_control_record_t test_load_record(void)
{
    ota_boot_control_storage_t storage = test_control_storage();
    ota_boot_control_record_t record;
    ota_control_copy_t copy;

    memset(&record, 0, sizeof(record));
    TEST_CHECK(ota_boot_control_storage_load(&storage, &record, &copy) ==
               OTA_CONTROL_STATUS_OK);
    return record;
}

bool gd25lq128_read_id(gd25lq128_id_t *id)
{
    (void)id;
    return true;
}

bool gd25lq128_read(uint32_t address, uint8_t *data, uint32_t size)
{
    uint32_t vectors[2] = {OTA_SRAM_BASE + 0x1000U, OTA_APP_BASE + 0x101U};

    if(address < sizeof(metadata))
    {
        return test_metadata_read(NULL, address, data, size) != 0U;
    }
    if((address == OTA_EXT_FIRMWARE_SLOT_A_ADDR ||
        address == OTA_EXT_FIRMWARE_SLOT_B_ADDR) && size == sizeof(vectors))
    {
        memcpy(data, vectors, sizeof(vectors));
        return true;
    }
    return false;
}

bool gd25lq128_erase_4k(uint32_t address)
{
    return test_metadata_erase(NULL, address) != 0U;
}

bool gd25lq128_write(uint32_t address, const uint8_t *data, uint32_t size)
{
    return test_metadata_write(NULL, address, data, size) != 0U;
}

bool gd25lq128_page_program(uint32_t address, const uint8_t *data, uint32_t size)
{
    return gd25lq128_write(address, data, size);
}

bool gd25lq128_read_verify(uint32_t address, const uint8_t *expected, uint32_t size)
{
    uint8_t buffer[512];
    if(size > sizeof(buffer) || !gd25lq128_read(address, buffer, size))
    {
        return false;
    }
    return memcmp(buffer, expected, size) == 0;
}

uint8_t ota_boot_internal_image_matches(uint32_t image_size, uint32_t image_crc32)
{
    return (internal_size == image_size && internal_crc == image_crc32) ? 1U : 0U;
}

uint8_t ota_boot_external_image_is_valid(
    uint32_t address,
    uint32_t image_size,
    uint32_t image_crc32)
{
    (void)image_size;
    (void)image_crc32;
    if(address == OTA_EXT_FIRMWARE_SLOT_A_ADDR)
    {
        return external_valid_a;
    }
    if(address == OTA_EXT_FIRMWARE_SLOT_B_ADDR)
    {
        return external_valid_b;
    }
    return 0U;
}

uint8_t ota_boot_install_external_image(
    uint32_t address,
    uint32_t image_size,
    uint32_t image_crc32)
{
    if(fail_install != 0U ||
       !ota_boot_external_image_is_valid(address, image_size, image_crc32))
    {
        return 0U;
    }
    internal_size = image_size;
    internal_crc = image_crc32;
    return 1U;
}

static void test_pending_install_and_trial_rollback(void)
{
    ota_boot_control_record_t record;

    test_reset();
    test_store_record(OTA_CONTROL_STATE_PENDING);
    internal_size = 1024U;
    internal_crc = 0xAAAA1111U;

    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_INSTALLED);
    record = test_load_record();
    TEST_CHECK(record.state == OTA_CONTROL_STATE_TRIAL);
    TEST_CHECK(record.trial_boot_count == 0U);
    TEST_CHECK(internal_crc == 0xBBBB2222U);

    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_NO_UPDATE);
    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_NO_UPDATE);
    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_ROLLED_BACK);
    record = test_load_record();
    TEST_CHECK(record.state == OTA_CONTROL_STATE_CONFIRMED);
    TEST_CHECK(record.active_slot == OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(record.pending_slot == OTA_FIRMWARE_SLOT_NONE);
    TEST_CHECK(internal_crc == 0xAAAA1111U);
}

static void test_installing_state_retries_after_power_loss(void)
{
    ota_boot_control_record_t record;

    test_reset();
    test_store_record(OTA_CONTROL_STATE_INSTALLING);
    internal_size = 1024U;
    internal_crc = 0xAAAA1111U;

    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_INSTALLED);
    record = test_load_record();
    TEST_CHECK(record.state == OTA_CONTROL_STATE_TRIAL);
    TEST_CHECK(internal_crc == 0xBBBB2222U);
}

static void test_failed_candidate_install_restores_active(void)
{
    ota_boot_control_record_t record;

    test_reset();
    test_store_record(OTA_CONTROL_STATE_PENDING);
    internal_size = 1024U;
    internal_crc = 0xAAAA1111U;
    external_valid_b = 0U;

    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_ROLLED_BACK);
    record = test_load_record();
    TEST_CHECK(record.state == OTA_CONTROL_STATE_CONFIRMED);
    TEST_CHECK(record.active_slot == OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(internal_crc == 0xAAAA1111U);
}

static void test_confirmed_internal_corruption_is_reinstalled(void)
{
    test_reset();
    test_store_record(OTA_CONTROL_STATE_CONFIRMED);
    internal_size = 0U;
    internal_crc = 0U;

    TEST_CHECK(ota_boot_v2_process() == OTA_BOOT_RESULT_INSTALLED);
    TEST_CHECK(internal_size == 1024U);
    TEST_CHECK(internal_crc == 0xAAAA1111U);
}

static void test_v1_confirmed_and_trial_migration(void)
{
    ota_manifest_t manifest;
    ota_boot_control_record_t record;

    test_reset();
    memset(&manifest, 0, sizeof(manifest));
    manifest.image_size = 1024U;
    manifest.image_version = 101U;
    manifest.image_crc32 = 0xBBBB2222U;
    manifest.package_address = OTA_EXT_FIRMWARE_SLOT_B_ADDR;
    manifest.load_address = OTA_APP_BASE;
    manifest.entry_address = OTA_APP_BASE + 0x101U;
    internal_size = manifest.image_size;
    internal_crc = manifest.image_crc32;

    TEST_CHECK(ota_boot_v2_migrate_confirmed_v1(&manifest) != 0U);
    record = test_load_record();
    TEST_CHECK(record.state == OTA_CONTROL_STATE_CONFIRMED);
    TEST_CHECK(record.active_slot == OTA_FIRMWARE_SLOT_B);

    test_reset();
    manifest.package_address = OTA_EXT_FIRMWARE_SLOT_A_ADDR;
    manifest.rollback_address = OTA_EXT_FIRMWARE_SLOT_B_ADDR;
    manifest.rollback_size = 1024U;
    manifest.rollback_crc32 = 0xAAAA1111U;
    internal_size = manifest.image_size;
    internal_crc = manifest.image_crc32;

    TEST_CHECK(ota_boot_v2_migrate_trial_v1(&manifest) != 0U);
    record = test_load_record();
    TEST_CHECK(record.state == OTA_CONTROL_STATE_TRIAL);
    TEST_CHECK(record.active_slot == OTA_FIRMWARE_SLOT_B);
    TEST_CHECK(record.pending_slot == OTA_FIRMWARE_SLOT_A);
}

int main(void)
{
    test_pending_install_and_trial_rollback();
    test_installing_state_retries_after_power_loss();
    test_failed_candidate_install_restores_active();
    test_confirmed_internal_corruption_is_reinstalled();
    test_v1_confirmed_and_trial_migration();

    if(test_failures != 0)
    {
        printf("ota_boot_v2: %d failure(s)\n", test_failures);
        return 1;
    }

    printf("ota_boot_v2: all tests passed\n");
    return 0;
}
