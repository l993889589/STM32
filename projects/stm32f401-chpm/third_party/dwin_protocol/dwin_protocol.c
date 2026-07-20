/**
 * @file dwin_protocol.c
 * @brief Pure-C DWIN DGUS frame construction, CRC, and acknowledgement parsing.
 */

#include "dwin_protocol.h"

#include <string.h>

/** @brief Calculate the CRC-16/Modbus value used by CRC-enabled DWIN frames. */
uint16_t dwin_protocol_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t index;
    unsigned int bit;

    if(data == NULL && length != 0U)
        return 0U;
    for(index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for(bit = 0U; bit < 8U; bit++)
        {
            if((crc & 1U) != 0U)
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            else
                crc >>= 1U;
        }
    }
    return crc;
}

/** @brief Build one CRC-enabled DWIN 0x82 variable-write frame. */
bool dwin_protocol_build_write(uint16_t address,
                               const uint8_t *payload,
                               uint16_t payload_length,
                               uint8_t *output,
                               uint16_t output_capacity,
                               uint16_t *output_length)
{
    uint16_t crc;
    uint16_t required;
    uint16_t position = 0U;

    if(output == NULL || output_length == NULL ||
       (payload == NULL && payload_length != 0U) ||
       payload_length > DWIN_PROTOCOL_MAX_WRITE_PAYLOAD)
    {
        return false;
    }
    required = (uint16_t)(payload_length + 8U);
    if(output_capacity < required)
        return false;

    output[position++] = 0x5AU;
    output[position++] = 0xA5U;
    output[position++] = (uint8_t)(payload_length + 5U);
    output[position++] = 0x82U;
    output[position++] = (uint8_t)(address >> 8);
    output[position++] = (uint8_t)address;
    if(payload_length > 0U)
    {
        memcpy(output + position, payload, payload_length);
        position = (uint16_t)(position + payload_length);
    }

    crc = dwin_protocol_crc16(output + 3U, (size_t)(position - 3U));
    output[position++] = (uint8_t)crc;
    output[position++] = (uint8_t)(crc >> 8);
    *output_length = position;
    return true;
}

/** @brief Classify a complete fixed DWIN 0x82 write acknowledgement. */
dwin_protocol_ack_t dwin_protocol_classify_ack(const uint8_t *frame,
                                                uint16_t length)
{
    static const uint8_t plain_ack[] =
        {0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU};
    static const uint8_t crc_ack[] =
        {0x5AU, 0xA5U, 0x05U, 0x82U, 0x4FU, 0x4BU, 0xA5U, 0xEFU};

    if(frame == NULL)
        return DWIN_PROTOCOL_ACK_NONE;
    if(length == sizeof(plain_ack) &&
       memcmp(frame, plain_ack, sizeof(plain_ack)) == 0)
    {
        return DWIN_PROTOCOL_ACK_PLAIN;
    }
    if(length == sizeof(crc_ack) &&
       memcmp(frame, crc_ack, sizeof(crc_ack)) == 0)
    {
        return DWIN_PROTOCOL_ACK_CRC;
    }
    return DWIN_PROTOCOL_ACK_NONE;
}
