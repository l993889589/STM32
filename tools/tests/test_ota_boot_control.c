/*
 * Host tests for the shared OTA boot-control record and atomic storage logic.
 * The fake NOR writer can stop after any byte to emulate power loss.
 */
#include "ota_boot_control.h"
#include <stdio.h>
#include <string.h>

#define TEST_FLASH_SIZE  (2U * OTA_EXT_SECTOR_SIZE)

typedef struct
{
    uint8_t bytes[TEST_FLASH_SIZE];
    int32_t write_budget;
    uint8_t fail_read_a;
    uint8_t fail_read_b;
} fake_flash_t;

static int test_failures;

#define TEST_CHECK(condition) \
    do { if(!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        test_failures++; \
    } } while(0)

static void fake_flash_init(fake_flash_t *flash)
{
    memset(flash->bytes, 0xFF, sizeof(flash->bytes));
    flash->write_budget = -1;
    flash->fail_read_a = 0U;
    flash->fail_read_b = 0U;
}

static uint8_t fake_flash_read(void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    fake_flash_t *flash = (fake_flash_t *)context;

    if((address == OTA_EXT_MANIFEST_A_ADDR && flash->fail_read_a != 0U) ||
       (address == OTA_EXT_MANIFEST_B_ADDR && flash->fail_read_b != 0U))
    {
        return 0U;
    }

    if(address + size > sizeof(flash->bytes))
    {
        return 0U;
    }

    memcpy(data, &flash->bytes[address], size);
    return 1U;
}

static uint8_t fake_flash_erase(void *context, uint32_t address)
{
    fake_flash_t *flash = (fake_flash_t *)context;

    if((address % OTA_EXT_SECTOR_SIZE) != 0U ||
       address + OTA_EXT_SECTOR_SIZE > sizeof(flash->bytes))
    {
        return 0U;
    }

    memset(&flash->bytes[address], 0xFF, OTA_EXT_SECTOR_SIZE);
    return 1U;
}

static uint8_t fake_flash_write(void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    fake_flash_t *flash = (fake_flash_t *)context;
    uint32_t writable = size;
    uint32_t index;

    if(address + size > sizeof(flash->bytes))
    {
        return 0U;
    }

    if(flash->write_budget >= 0 && (uint32_t)flash->write_budget < writable)
    {
        writable = (uint32_t)flash->write_budget;
    }

    for(index = 0U; index < writable; index++)
    {
        flash->bytes[address + index] &= data[index];
    }

    if(flash->write_budget >= 0)
    {
        flash->write_budget -= (int32_t)writable;
    }

    return (writable == size) ? 1U : 0U;
}

static ota_boot_control_storage_t fake_storage(fake_flash_t *flash)
{
    ota_boot_control_storage_t storage;

    storage.context = flash;
    storage.read = fake_flash_read;
    storage.erase_sector = fake_flash_erase;
    storage.write = fake_flash_write;
    return storage;
}

static void set_downloading_record(ota_boot_control_record_t *record, uint32_t slot)
{
    ota_boot_control_init(record);
    record->state = OTA_CONTROL_STATE_DOWNLOADING;
    record->pending_slot = slot;
    record->slots[slot].state = OTA_SLOT_STATE_DOWNLOADING;
    ota_boot_control_prepare(record);
}

static void test_record_validation(void)
{
    ota_boot_control_record_t record;

    ota_boot_control_init(&record);
    TEST_CHECK(ota_boot_control_body_is_valid(&record) != 0U);
    TEST_CHECK(ota_boot_control_is_valid(&record) == 0U);

    ota_boot_control_mark_committed(&record);
    TEST_CHECK(ota_boot_control_is_valid(&record) != 0U);

    record.flags ^= 1U;
    TEST_CHECK(ota_boot_control_is_valid(&record) == 0U);
}

static void test_transition_table(void)
{
    TEST_CHECK(ota_boot_control_transition_is_allowed(
        OTA_CONTROL_STATE_CONFIRMED, OTA_CONTROL_STATE_DOWNLOADING) != 0U);
    TEST_CHECK(ota_boot_control_transition_is_allowed(
        OTA_CONTROL_STATE_PENDING, OTA_CONTROL_STATE_INSTALLING) != 0U);
    TEST_CHECK(ota_boot_control_transition_is_allowed(
        OTA_CONTROL_STATE_TRIAL, OTA_CONTROL_STATE_ROLLBACK) != 0U);
    TEST_CHECK(ota_boot_control_transition_is_allowed(
        OTA_CONTROL_STATE_CONFIRMED, OTA_CONTROL_STATE_TRIAL) == 0U);
    TEST_CHECK(ota_boot_control_transition_is_allowed(
        OTA_CONTROL_STATE_INSTALLING, OTA_CONTROL_STATE_DOWNLOADING) == 0U);
}

static void test_sequence_wrap_selection(void)
{
    ota_boot_control_record_t copy_a;
    ota_boot_control_record_t copy_b;
    ota_boot_control_record_t selected;
    ota_control_copy_t source;

    ota_boot_control_init(&copy_a);
    copy_a.sequence = 0xFFFFFFFFUL;
    ota_boot_control_prepare(&copy_a);
    ota_boot_control_mark_committed(&copy_a);

    ota_boot_control_init(&copy_b);
    copy_b.sequence = 0U;
    ota_boot_control_prepare(&copy_b);
    ota_boot_control_mark_committed(&copy_b);

    TEST_CHECK(ota_boot_control_select(&copy_a, &copy_b, &selected, &source) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(source == OTA_CONTROL_COPY_B);
}

static void test_store_and_load(void)
{
    fake_flash_t flash;
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t requested;
    ota_boot_control_record_t committed;
    ota_boot_control_record_t loaded;
    ota_control_copy_t copy;

    fake_flash_init(&flash);
    storage = fake_storage(&flash);
    set_downloading_record(&requested, OTA_FIRMWARE_SLOT_A);

    TEST_CHECK(ota_boot_control_storage_store(&storage, &requested, &committed, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(copy == OTA_CONTROL_COPY_A);
    TEST_CHECK(committed.sequence == 1U);
    TEST_CHECK(ota_boot_control_storage_load(&storage, &loaded, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(copy == OTA_CONTROL_COPY_A);
    TEST_CHECK(loaded.pending_slot == OTA_FIRMWARE_SLOT_A);
}

static void test_one_unreadable_copy_uses_the_other(void)
{
    fake_flash_t flash;
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t requested;
    ota_boot_control_record_t committed;
    ota_boot_control_record_t loaded;
    ota_control_copy_t copy;

    fake_flash_init(&flash);
    storage = fake_storage(&flash);
    set_downloading_record(&requested, OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(ota_boot_control_storage_store(&storage, &requested, &committed, &copy) ==
               OTA_CONTROL_STATUS_OK);

    set_downloading_record(&requested, OTA_FIRMWARE_SLOT_B);
    TEST_CHECK(ota_boot_control_storage_store(&storage, &requested, &committed, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(copy == OTA_CONTROL_COPY_B);

    flash.fail_read_b = 1U;
    TEST_CHECK(ota_boot_control_storage_load(&storage, &loaded, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(copy == OTA_CONTROL_COPY_A);
    TEST_CHECK(loaded.pending_slot == OTA_FIRMWARE_SLOT_A);
}

static void test_all_torn_write_offsets_preserve_old_copy(void)
{
    fake_flash_t baseline;
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t old_record;
    ota_boot_control_record_t new_record;
    ota_boot_control_record_t committed;
    ota_boot_control_record_t loaded;
    ota_control_copy_t copy;
    uint32_t cut;
    const uint32_t total_write_size =
        (uint32_t)offsetof(ota_boot_control_record_t, commit_marker) + sizeof(uint32_t);

    fake_flash_init(&baseline);
    storage = fake_storage(&baseline);
    set_downloading_record(&old_record, OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(ota_boot_control_storage_store(&storage, &old_record, &committed, &copy) ==
               OTA_CONTROL_STATUS_OK);

    set_downloading_record(&new_record, OTA_FIRMWARE_SLOT_B);

    for(cut = 0U; cut < total_write_size; cut++)
    {
        fake_flash_t trial = baseline;
        ota_boot_control_storage_t trial_storage = fake_storage(&trial);

        trial.write_budget = (int32_t)cut;
        TEST_CHECK(ota_boot_control_storage_store(
            &trial_storage, &new_record, &committed, &copy) != OTA_CONTROL_STATUS_OK);
        TEST_CHECK(ota_boot_control_storage_load(&trial_storage, &loaded, &copy) ==
                   OTA_CONTROL_STATUS_OK);
        TEST_CHECK(copy == OTA_CONTROL_COPY_A);
        TEST_CHECK(loaded.pending_slot == OTA_FIRMWARE_SLOT_A);
        TEST_CHECK(loaded.sequence == 1U);
    }
}

int main(void)
{
    test_record_validation();
    test_transition_table();
    test_sequence_wrap_selection();
    test_store_and_load();
    test_one_unreadable_copy_uses_the_other();
    test_all_torn_write_offsets_preserve_old_copy();

    if(test_failures != 0)
    {
        printf("ota_boot_control: %d failure(s)\n", test_failures);
        return 1;
    }

    printf("ota_boot_control: all tests passed\n");
    return 0;
}
