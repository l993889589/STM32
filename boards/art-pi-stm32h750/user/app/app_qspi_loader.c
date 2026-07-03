#include "app_qspi_loader.h"

#include <string.h>

#include "bsp_qspi_flash.h"

#define QSPI_FRAME_HEADER_SIZE 17U

#define QSPI_CMD_INFO          1U
#define QSPI_CMD_ERASE         2U
#define QSPI_CMD_WRITE         3U
#define QSPI_CMD_CRC           4U

#define QSPI_STATUS_OK         0U
#define QSPI_STATUS_BAD_FRAME  1U
#define QSPI_STATUS_BAD_CRC    2U
#define QSPI_STATUS_BAD_RANGE  3U
#define QSPI_STATUS_FLASH      4U

volatile uint32_t app_qspi_loader_frames;
volatile uint32_t app_qspi_loader_writes;
volatile uint32_t app_qspi_loader_last_status;
volatile uint32_t app_qspi_loader_last_address;
volatile uint32_t app_qspi_loader_last_value;

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    while(length-- != 0U)
    {
        crc ^= *data++;
        for(uint32_t bit = 0U; bit < 8U; bit++)
            crc = (crc & 1UL) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }

    return crc ^ 0xFFFFFFFFUL;
}

static bool range_allowed(uint32_t address, uint32_t length)
{
    return length != 0U &&
           address >= APP_QSPI_LOADER_BASE &&
           length <= (APP_QSPI_LOADER_BASE + APP_QSPI_LOADER_SIZE - address);
}

static void send_ack(app_serial_ldc_t *serial,
                     uint8_t cmd,
                     uint16_t seq,
                     uint8_t status,
                     uint32_t address,
                     uint32_t value,
                     uint32_t extra)
{
    uint8_t ack[20];

    ack[0] = 'Q';
    ack[1] = 'A';
    ack[2] = 'C';
    ack[3] = 'K';
    ack[4] = cmd;
    ack[5] = status;
    put_u16_le(&ack[6], seq);
    put_u32_le(&ack[8], address);
    put_u32_le(&ack[12], value);
    put_u32_le(&ack[16], extra);

    app_qspi_loader_last_status = status;
    app_qspi_loader_last_address = address;
    app_qspi_loader_last_value = value;

    (void)app_serial_ldc_send_async(serial, ack, sizeof(ack));
}

bool app_qspi_loader_handle_frame(app_serial_ldc_t *serial,
                                  const uint8_t *frame,
                                  uint16_t length)
{
    uint8_t cmd;
    uint16_t seq;
    uint32_t address;
    uint32_t len_or_range;
    uint32_t payload_crc;
    const uint8_t *payload;
    uint16_t payload_length;
    uint8_t status = QSPI_STATUS_OK;
    uint32_t value = 0U;
    uint32_t extra = 0U;

    if(serial == NULL || frame == NULL || length < 4U)
        return false;

    if(frame[0] != 'Q' || frame[1] != 'S' || frame[2] != 'P' || frame[3] != 'I')
        return false;

    app_qspi_loader_frames++;

    if(length < QSPI_FRAME_HEADER_SIZE)
    {
        send_ack(serial, 0U, 0U, QSPI_STATUS_BAD_FRAME, 0U, 0U, 0U);
        return true;
    }

    cmd = frame[4];
    seq = get_u16_le(&frame[5]);
    address = get_u32_le(&frame[7]);
    len_or_range = get_u16_le(&frame[11]);
    payload_crc = get_u32_le(&frame[13]);
    payload = &frame[QSPI_FRAME_HEADER_SIZE];
    payload_length = (uint16_t)(length - QSPI_FRAME_HEADER_SIZE);

    switch(cmd)
    {
    case QSPI_CMD_INFO:
        if(payload_length != 0U)
        {
            status = QSPI_STATUS_BAD_FRAME;
            break;
        }
        value = APP_QSPI_LOADER_BASE;
        extra = APP_QSPI_LOADER_SIZE;
        break;

    case QSPI_CMD_ERASE:
        if(payload_length != 0U || !range_allowed(address, len_or_range))
        {
            status = QSPI_STATUS_BAD_RANGE;
            break;
        }
        if(bsp_qspi_flash_erase(address, len_or_range) != 0)
            status = QSPI_STATUS_FLASH;
        value = len_or_range;
        break;

    case QSPI_CMD_WRITE:
        if(payload_length != len_or_range ||
           payload_length == 0U ||
           payload_length > APP_QSPI_LOADER_MAX_PAYLOAD ||
           !range_allowed(address, payload_length))
        {
            status = QSPI_STATUS_BAD_RANGE;
            break;
        }
        if(crc32_bytes(payload, payload_length) != payload_crc)
        {
            status = QSPI_STATUS_BAD_CRC;
            break;
        }
        if(bsp_qspi_flash_write(address, payload, payload_length) != 0)
            status = QSPI_STATUS_FLASH;
        else
            app_qspi_loader_writes++;
        value = payload_length;
        break;

    case QSPI_CMD_CRC:
        if(payload_length != 0U || !range_allowed(address, len_or_range))
        {
            status = QSPI_STATUS_BAD_RANGE;
            break;
        }
        value = bsp_qspi_flash_crc32(address, len_or_range);
        extra = len_or_range;
        break;

    default:
        status = QSPI_STATUS_BAD_FRAME;
        break;
    }

    send_ack(serial, cmd, seq, status, address, value, extra);
    return true;
}
