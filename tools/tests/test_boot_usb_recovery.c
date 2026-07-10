/*
 * Host tests for Boot CDC LDOT recovery.
 *
 * Tests fragmented and coalesced binary frames against a fake NOR device and
 * verifies that FINISH publishes a first-install PENDING record.
 */
#include "boot_usb_recovery.h"
#include "ota_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_FLASH_SIZE OTA_EXT_DIAGNOSTIC_ADDR
#define TEST_IMAGE_SIZE 448U
#define TEST_FRAME_MAX  242U

typedef struct
{
    uint8_t *bytes;
} test_flash_t;

static int g_failures;
static unsigned int g_ack_command;
static unsigned int g_ack_sequence;
static unsigned int g_ack_status;
static uint32_t g_reset_count;

#define TEST_CHECK(condition) \
    do { if(!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        g_failures++; \
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

static uint16_t test_crc16(const uint8_t *data, uint32_t size)
{
    uint16_t crc = 0xFFFFU;
    while(size-- != 0U)
    {
        uint32_t bit;
        crc ^= *data++;
        for(bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                  (uint16_t)((crc >> 1U) ^ 0xA001U) :
                  (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static uint8_t test_read(void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    test_flash_t *flash = (test_flash_t *)context;
    if(address > TEST_FLASH_SIZE || size > TEST_FLASH_SIZE - address)
    {
        return 0U;
    }
    memcpy(data, &flash->bytes[address], size);
    return 1U;
}

static uint8_t test_erase(void *context, uint32_t address)
{
    test_flash_t *flash = (test_flash_t *)context;
    if((address % OTA_EXT_SECTOR_SIZE) != 0U ||
       address > TEST_FLASH_SIZE - OTA_EXT_SECTOR_SIZE)
    {
        return 0U;
    }
    memset(&flash->bytes[address], 0xFF, OTA_EXT_SECTOR_SIZE);
    return 1U;
}

static uint8_t test_write(
    void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    test_flash_t *flash = (test_flash_t *)context;
    uint32_t index;
    if(address > TEST_FLASH_SIZE || size > TEST_FLASH_SIZE - address)
    {
        return 0U;
    }
    for(index = 0U; index < size; index++)
    {
        flash->bytes[address + index] &= data[index];
    }
    return 1U;
}

static int test_ack_write(const uint8_t *data, uint16_t size, void *context)
{
    char line[48];
    (void)context;
    if(size >= sizeof(line))
    {
        return -1;
    }
    memcpy(line, data, size);
    line[size] = '\0';
    if(sscanf(line, "ota ack %u %u %u", &g_ack_command,
              &g_ack_sequence, &g_ack_status) != 3)
    {
        return -1;
    }
    return (int)size;
}

static void test_reset(void *context)
{
    (void)context;
    g_reset_count++;
}

static void test_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void test_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t test_make_frame(
    uint8_t *frame,
    uint8_t command,
    uint16_t sequence,
    uint32_t offset,
    const uint8_t *payload,
    uint16_t payload_size)
{
    uint32_t total = 16U + payload_size + 2U;
    memset(frame, 0, total);
    frame[0] = 'L';
    frame[1] = 'D';
    frame[2] = 'O';
    frame[3] = 'T';
    frame[4] = command;
    test_put_u16(&frame[6], sequence);
    test_put_u32(&frame[8], offset);
    test_put_u16(&frame[12], payload_size);
    if(payload_size != 0U)
    {
        memcpy(&frame[16], payload, payload_size);
    }
    test_put_u16(&frame[16U + payload_size], test_crc16(frame, 16U + payload_size));
    return total;
}

static void test_first_install_and_reset(void)
{
    test_flash_t flash;
    ota_firmware_update_storage_t storage;
    ota_boot_control_storage_t control_storage;
    ota_boot_control_record_t control;
    ota_firmware_descriptor_t descriptor;
    ota_control_copy_t copy;
    uint8_t image[TEST_IMAGE_SIZE];
    uint8_t begin_frame[TEST_FRAME_MAX];
    uint8_t data_frames[TEST_FRAME_MAX * 2U];
    uint8_t finish_frame[TEST_FRAME_MAX];
    uint8_t reset_frame[TEST_FRAME_MAX];
    uint32_t begin_size;
    uint32_t data_size_a;
    uint32_t data_size_b;
    uint32_t finish_size;
    uint32_t reset_size;
    uint32_t index;

    flash.bytes = (uint8_t *)malloc(TEST_FLASH_SIZE);
    TEST_CHECK(flash.bytes != NULL);
    if(flash.bytes == NULL)
    {
        return;
    }
    memset(flash.bytes, 0xFF, TEST_FLASH_SIZE);
    for(index = 0U; index < sizeof(image); index++)
    {
        image[index] = (uint8_t)(index * 13U + 7U);
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.state = OTA_SLOT_STATE_VERIFIED;
    descriptor.image_version = 2026071017U;
    descriptor.image_size = sizeof(image);
    descriptor.image_crc32 = test_crc32(0U, image, sizeof(image));
    ota_sha256_calculate(image, sizeof(image), descriptor.image_sha256);
    descriptor.load_address = OTA_APP_BASE;
    descriptor.entry_address = OTA_APP_BASE + 0x101U;

    storage.context = &flash;
    storage.read = test_read;
    storage.erase_sector = test_erase;
    storage.write = test_write;
    TEST_CHECK(boot_usb_recovery_init(&storage, test_reset, NULL) ==
               OTA_FIRMWARE_UPDATE_OK);

    begin_size = test_make_frame(begin_frame, 32U, 1U, 0U,
                                 (const uint8_t *)&descriptor,
                                 (uint16_t)sizeof(descriptor));
    TEST_CHECK(boot_usb_recovery_feed(begin_frame, 1U, test_ack_write, NULL) != 0U);
    TEST_CHECK(boot_usb_recovery_feed(&begin_frame[1], 2U, test_ack_write, NULL) != 0U);
    TEST_CHECK(boot_usb_recovery_feed(&begin_frame[3], begin_size - 3U,
                                      test_ack_write, NULL) != 0U);
    TEST_CHECK(g_ack_command == 32U && g_ack_sequence == 1U && g_ack_status == 0U);

    data_size_a = test_make_frame(data_frames, 33U, 2U, 0U, image, 224U);
    data_size_b = test_make_frame(&data_frames[data_size_a], 33U, 3U, 224U,
                                  &image[224], 224U);
    TEST_CHECK(boot_usb_recovery_feed(data_frames, data_size_a + data_size_b,
                                      test_ack_write, NULL) != 0U);
    TEST_CHECK(g_ack_command == 33U && g_ack_sequence == 3U && g_ack_status == 0U);

    finish_size = test_make_frame(finish_frame, 34U, 4U, 0U, NULL, 0U);
    TEST_CHECK(boot_usb_recovery_feed(finish_frame, finish_size,
                                      test_ack_write, NULL) != 0U);
    TEST_CHECK(g_ack_command == 34U && g_ack_sequence == 4U && g_ack_status == 0U);

    control_storage.context = &flash;
    control_storage.read = test_read;
    control_storage.erase_sector = test_erase;
    control_storage.write = test_write;
    TEST_CHECK(ota_boot_control_storage_load(&control_storage, &control, &copy) ==
               OTA_CONTROL_STATUS_OK);
    TEST_CHECK(control.state == OTA_CONTROL_STATE_PENDING);
    TEST_CHECK(control.active_slot == OTA_FIRMWARE_SLOT_NONE);
    TEST_CHECK(control.pending_slot == OTA_FIRMWARE_SLOT_A);
    TEST_CHECK(memcmp(&flash.bytes[OTA_EXT_FIRMWARE_SLOT_A_ADDR], image,
                      sizeof(image)) == 0);

    reset_size = test_make_frame(reset_frame, 5U, 5U, 0U, NULL, 0U);
    TEST_CHECK(boot_usb_recovery_feed(reset_frame, reset_size,
                                      test_ack_write, NULL) != 0U);
    TEST_CHECK(g_ack_status == 0U);
    TEST_CHECK(g_reset_count == 1U);
    free(flash.bytes);
}

int main(void)
{
    test_first_install_and_reset();
    if(g_failures != 0)
    {
        printf("boot_usb_recovery: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("boot_usb_recovery: all tests passed\n");
    return 0;
}
