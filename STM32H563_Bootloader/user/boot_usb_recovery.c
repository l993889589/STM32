/*
 * Bootloader LDOT recovery implementation.
 *
 * Framing is length-based and binary-safe. The parser never uses CR/LF as a
 * delimiter, and every frame is protected by CRC16 before it reaches flash.
 */
#include "boot_usb_recovery.h"

#include <stdio.h>
#include <string.h>

#if !defined(BOOT_USB_RECOVERY_HOST_TEST)
#include "gd25lq128.h"
#include "main.h"
#endif

#define BOOT_LDOT_MAGIC_0          0x4CU
#define BOOT_LDOT_MAGIC_1          0x44U
#define BOOT_LDOT_MAGIC_2          0x4FU
#define BOOT_LDOT_MAGIC_3          0x54U
#define BOOT_LDOT_HEADER_SIZE      16U
#define BOOT_LDOT_MAX_PAYLOAD      224U
#define BOOT_LDOT_BUFFER_SIZE      (BOOT_LDOT_HEADER_SIZE + BOOT_LDOT_MAX_PAYLOAD + 2U)

#define BOOT_LDOT_CMD_RESET        5U
#define BOOT_LDOT_CMD_FW_BEGIN     32U
#define BOOT_LDOT_CMD_FW_DATA      33U
#define BOOT_LDOT_CMD_FW_FINISH    34U
#define BOOT_LDOT_CMD_FW_ABORT     35U

#define BOOT_LDOT_STATUS_OK          0U
#define BOOT_LDOT_STATUS_BAD_FRAME   1U
#define BOOT_LDOT_STATUS_BAD_CRC     2U
#define BOOT_LDOT_STATUS_BAD_RANGE   3U
#define BOOT_LDOT_STATUS_FLASH_ERROR 4U
#define BOOT_LDOT_STATUS_SEQUENCE    5U
#define BOOT_LDOT_STATUS_NOT_READY   6U
#define BOOT_LDOT_STATUS_IMAGE_ERROR 7U

static ota_firmware_update_t g_update;
static uint8_t g_stream[BOOT_LDOT_BUFFER_SIZE];
static uint32_t g_stream_size;
static uint32_t g_expected_size;
static uint32_t g_received_size;
static uint16_t g_expected_sequence;
static uint8_t g_active;
static uint8_t g_initialized;
static boot_usb_recovery_reset_fn g_reset;
static void *g_reset_context;

static uint16_t boot_usb_recovery_get_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t boot_usb_recovery_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static uint16_t boot_usb_recovery_crc16(const uint8_t *data, uint32_t size)
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

static uint8_t boot_usb_recovery_status(ota_firmware_update_status_t status)
{
    switch(status)
    {
    case OTA_FIRMWARE_UPDATE_OK:
        return BOOT_LDOT_STATUS_OK;
    case OTA_FIRMWARE_UPDATE_NOT_PROVISIONED:
    case OTA_FIRMWARE_UPDATE_BUSY:
    case OTA_FIRMWARE_UPDATE_BAD_STATE:
        return BOOT_LDOT_STATUS_NOT_READY;
    case OTA_FIRMWARE_UPDATE_BAD_RANGE:
        return BOOT_LDOT_STATUS_BAD_RANGE;
    case OTA_FIRMWARE_UPDATE_SEQUENCE:
        return BOOT_LDOT_STATUS_SEQUENCE;
    case OTA_FIRMWARE_UPDATE_CRC_MISMATCH:
    case OTA_FIRMWARE_UPDATE_SHA256_MISMATCH:
    case OTA_FIRMWARE_UPDATE_VERIFY_FAILED:
        return BOOT_LDOT_STATUS_IMAGE_ERROR;
    default:
        return BOOT_LDOT_STATUS_FLASH_ERROR;
    }
}

static void boot_usb_recovery_ack(
    uint8_t command,
    uint16_t sequence,
    uint8_t status,
    boot_usb_recovery_write_fn write,
    void *write_context)
{
    char line[40];
    int length;

    if(write == NULL)
    {
        return;
    }
    length = snprintf(line, sizeof(line), "ota ack %u %u %u\r\n",
                      (unsigned int)command,
                      (unsigned int)sequence,
                      (unsigned int)status);
    if(length > 0 && (uint32_t)length < sizeof(line))
    {
        (void)write((const uint8_t *)line, (uint16_t)length, write_context);
    }
}

static uint8_t boot_usb_recovery_handle(
    uint8_t command,
    uint16_t sequence,
    uint32_t offset,
    const uint8_t *payload,
    uint16_t payload_size,
    uint8_t *reset_requested)
{
    ota_firmware_update_status_t update_status = OTA_FIRMWARE_UPDATE_OK;
    uint8_t status = BOOT_LDOT_STATUS_OK;

    switch(command)
    {
    case BOOT_LDOT_CMD_FW_BEGIN:
    {
        ota_firmware_descriptor_t descriptor;
        if(g_initialized == 0U)
        {
            status = BOOT_LDOT_STATUS_NOT_READY;
            break;
        }
        if(payload_size != sizeof(descriptor))
        {
            status = BOOT_LDOT_STATUS_BAD_FRAME;
            break;
        }
        memcpy(&descriptor, payload, sizeof(descriptor));
        update_status = ota_firmware_update_begin_recovery(&g_update, &descriptor);
        status = boot_usb_recovery_status(update_status);
        if(status == BOOT_LDOT_STATUS_OK)
        {
            g_expected_size = descriptor.image_size;
            g_received_size = 0U;
            g_expected_sequence = (uint16_t)(sequence + 1U);
            g_active = 1U;
        }
        break;
    }

    case BOOT_LDOT_CMD_FW_DATA:
        if(g_active == 0U || sequence != g_expected_sequence ||
           offset != g_received_size || payload_size == 0U)
        {
            status = BOOT_LDOT_STATUS_SEQUENCE;
            break;
        }
        update_status = ota_firmware_update_write(
            &g_update, offset, payload, payload_size);
        status = boot_usb_recovery_status(update_status);
        if(status == BOOT_LDOT_STATUS_OK)
        {
            g_received_size += payload_size;
            g_expected_sequence++;
        }
        break;

    case BOOT_LDOT_CMD_FW_FINISH:
        if(g_active == 0U || payload_size != 0U ||
           g_received_size != g_expected_size)
        {
            status = BOOT_LDOT_STATUS_SEQUENCE;
            break;
        }
        status = boot_usb_recovery_status(ota_firmware_update_finish(&g_update));
        if(status == BOOT_LDOT_STATUS_OK)
        {
            g_active = 0U;
        }
        break;

    case BOOT_LDOT_CMD_FW_ABORT:
        if(payload_size != 0U)
        {
            status = BOOT_LDOT_STATUS_BAD_FRAME;
            break;
        }
        status = boot_usb_recovery_status(ota_firmware_update_abort(&g_update));
        g_active = 0U;
        break;

    case BOOT_LDOT_CMD_RESET:
        if(payload_size != 0U || g_active != 0U)
        {
            status = BOOT_LDOT_STATUS_SEQUENCE;
        }
        else
        {
            *reset_requested = 1U;
        }
        break;

    default:
        status = BOOT_LDOT_STATUS_BAD_FRAME;
        break;
    }

    if(status != BOOT_LDOT_STATUS_OK &&
       command >= BOOT_LDOT_CMD_FW_BEGIN &&
       command <= BOOT_LDOT_CMD_FW_FINISH)
    {
        (void)ota_firmware_update_abort(&g_update);
        g_active = 0U;
    }
    return status;
}

ota_firmware_update_status_t boot_usb_recovery_init(
    const ota_firmware_update_storage_t *storage,
    boot_usb_recovery_reset_fn reset,
    void *reset_context)
{
    ota_firmware_update_status_t status;

    memset(&g_update, 0, sizeof(g_update));
    g_stream_size = 0U;
    g_expected_size = 0U;
    g_received_size = 0U;
    g_expected_sequence = 0U;
    g_active = 0U;
    g_initialized = 0U;
    g_reset = reset;
    g_reset_context = reset_context;

    status = ota_firmware_update_init(&g_update, storage);
    if(status == OTA_FIRMWARE_UPDATE_OK)
    {
        g_initialized = 1U;
    }
    return status;
}

#if !defined(BOOT_USB_RECOVERY_HOST_TEST)
static uint8_t boot_usb_recovery_read(
    void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    (void)context;
    return gd25lq128_read(address, data, size) ? 1U : 0U;
}

static uint8_t boot_usb_recovery_erase(void *context, uint32_t address)
{
    (void)context;
    return gd25lq128_erase_4k(address) ? 1U : 0U;
}

static uint8_t boot_usb_recovery_write(
    void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    (void)context;
    return gd25lq128_write(address, data, size) ? 1U : 0U;
}

static void boot_usb_recovery_reset(void *context)
{
    (void)context;
    NVIC_SystemReset();
}
#endif

ota_firmware_update_status_t boot_usb_recovery_init_default(void)
{
#if defined(BOOT_USB_RECOVERY_HOST_TEST)
    return OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT;
#else
    ota_firmware_update_storage_t storage;
    storage.context = NULL;
    storage.read = boot_usb_recovery_read;
    storage.erase_sector = boot_usb_recovery_erase;
    storage.write = boot_usb_recovery_write;
    return boot_usb_recovery_init(&storage, boot_usb_recovery_reset, NULL);
#endif
}

uint8_t boot_usb_recovery_feed(
    const uint8_t *data,
    uint32_t size,
    boot_usb_recovery_write_fn write,
    void *write_context)
{
    uint8_t handled = 0U;

    if(data == NULL || size == 0U)
    {
        return 0U;
    }

    while(size != 0U)
    {
        uint32_t available;
        uint32_t copy_size;

        if(g_stream_size == 0U && data[0] != BOOT_LDOT_MAGIC_0)
        {
            return handled;
        }

        handled = 1U;
        available = sizeof(g_stream) - g_stream_size;
        copy_size = (size < available) ? size : available;
        memcpy(&g_stream[g_stream_size], data, copy_size);
        g_stream_size += copy_size;
        data += copy_size;
        size -= copy_size;

        if(g_stream_size >= 4U &&
           (g_stream[0] != BOOT_LDOT_MAGIC_0 ||
            g_stream[1] != BOOT_LDOT_MAGIC_1 ||
            g_stream[2] != BOOT_LDOT_MAGIC_2 ||
            g_stream[3] != BOOT_LDOT_MAGIC_3))
        {
            boot_usb_recovery_ack(0U, 0U, BOOT_LDOT_STATUS_BAD_FRAME,
                                  write, write_context);
            g_stream_size = 0U;
            return 1U;
        }

        if(g_stream_size >= BOOT_LDOT_HEADER_SIZE)
        {
            uint8_t command = g_stream[4];
            uint16_t sequence = boot_usb_recovery_get_u16(&g_stream[6]);
            uint16_t payload_size = boot_usb_recovery_get_u16(&g_stream[12]);
            uint32_t total_size = BOOT_LDOT_HEADER_SIZE + payload_size + 2U;

            if(payload_size > BOOT_LDOT_MAX_PAYLOAD || total_size > sizeof(g_stream))
            {
                boot_usb_recovery_ack(command, sequence, BOOT_LDOT_STATUS_BAD_FRAME,
                                      write, write_context);
                g_stream_size = 0U;
                return 1U;
            }

            if(g_stream_size >= total_size)
            {
                uint16_t frame_crc = boot_usb_recovery_get_u16(
                    &g_stream[BOOT_LDOT_HEADER_SIZE + payload_size]);
                uint16_t calculated_crc = boot_usb_recovery_crc16(
                    g_stream, BOOT_LDOT_HEADER_SIZE + payload_size);
                uint8_t reset_requested = 0U;
                uint8_t status;

                if(frame_crc != calculated_crc)
                {
                    status = BOOT_LDOT_STATUS_BAD_CRC;
                }
                else
                {
                    status = boot_usb_recovery_handle(
                        command,
                        sequence,
                        boot_usb_recovery_get_u32(&g_stream[8]),
                        &g_stream[BOOT_LDOT_HEADER_SIZE],
                        payload_size,
                        &reset_requested);
                }
                boot_usb_recovery_ack(command, sequence, status, write, write_context);
                g_stream_size = 0U;

                if(reset_requested != 0U && g_reset != NULL)
                {
                    g_reset(g_reset_context);
                }
            }
            else if(available == 0U)
            {
                g_stream_size = 0U;
                return 1U;
            }
        }
    }

    return handled;
}

void boot_usb_recovery_get_progress(
    uint8_t *active,
    uint32_t *target_slot,
    uint32_t *received_size,
    uint32_t *expected_size)
{
    if(active != NULL)
    {
        *active = g_active;
    }
    if(target_slot != NULL)
    {
        *target_slot = g_update.target_slot;
    }
    if(received_size != NULL)
    {
        *received_size = g_received_size;
    }
    if(expected_size != NULL)
    {
        *expected_size = g_expected_size;
    }
}
