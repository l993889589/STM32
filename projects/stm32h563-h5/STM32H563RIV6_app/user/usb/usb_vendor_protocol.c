#include "usb_vendor_protocol.h"

#include <stddef.h>
#include <string.h>

static const uint8_t g_vendor_magic[4] = {'L', 'D', 'V', '1'};

static uint16_t get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
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

uint32_t usb_vendor_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    if(data == NULL && length != 0U)
        return 0U;

    while(length-- != 0U)
    {
        uint8_t bit;
        crc ^= *data++;
        for(bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFUL;
}

void usb_vendor_parser_init(usb_vendor_parser_t *parser,
                            usb_vendor_frame_fn handler,
                            void *handler_arg)
{
    if(parser == NULL)
        return;
    memset(parser, 0, sizeof(*parser));
    parser->handler = handler;
    parser->handler_arg = handler_arg;
}

static void parser_discard(usb_vendor_parser_t *parser, uint32_t count)
{
    if(count >= parser->length)
    {
        parser->discarded_bytes += parser->length;
        parser->length = 0U;
        return;
    }

    memmove(parser->buffer, &parser->buffer[count], parser->length - count);
    parser->length -= count;
    parser->discarded_bytes += count;
}

static bool parser_align_magic(usb_vendor_parser_t *parser)
{
    uint32_t offset = 0U;

    while(offset + sizeof(g_vendor_magic) <= parser->length)
    {
        if(memcmp(&parser->buffer[offset], g_vendor_magic, sizeof(g_vendor_magic)) == 0)
        {
            if(offset != 0U)
                parser_discard(parser, offset);
            return true;
        }
        offset++;
    }

    if(parser->length > 3U)
        parser_discard(parser, parser->length - 3U);
    return false;
}

void usb_vendor_parser_feed(usb_vendor_parser_t *parser,
                            const uint8_t *data,
                            uint32_t length)
{
    if(parser == NULL || data == NULL || length == 0U)
        return;

    while(length != 0U)
    {
        uint32_t room = sizeof(parser->buffer) - parser->length;
        uint32_t copy = (length < room) ? length : room;

        if(copy == 0U)
        {
            parser->length_errors++;
            parser_discard(parser, 1U);
            continue;
        }

        memcpy(&parser->buffer[parser->length], data, copy);
        parser->length += copy;
        data += copy;
        length -= copy;

        for(;;)
        {
            uint32_t payload_length;
            uint32_t frame_length;
            uint32_t received_crc;
            usb_vendor_frame_t frame;

            if(parser->length < sizeof(g_vendor_magic) || !parser_align_magic(parser))
                break;
            if(parser->length < USB_VENDOR_HEADER_SIZE)
                break;

            payload_length = get_u32_le(&parser->buffer[8]);
            if(payload_length > USB_VENDOR_MAX_PAYLOAD)
            {
                parser->length_errors++;
                parser_discard(parser, 1U);
                continue;
            }

            frame_length = USB_VENDOR_HEADER_SIZE + payload_length;
            if(parser->length < frame_length)
                break;

            received_crc = get_u32_le(&parser->buffer[12]);
            if(received_crc != usb_vendor_crc32(&parser->buffer[USB_VENDOR_HEADER_SIZE], payload_length))
            {
                parser->crc_errors++;
                parser_discard(parser, frame_length);
                continue;
            }

            frame.channel = parser->buffer[4];
            frame.flags = parser->buffer[5];
            frame.sequence = get_u16_le(&parser->buffer[6]);
            frame.payload = &parser->buffer[USB_VENDOR_HEADER_SIZE];
            frame.payload_length = payload_length;
            parser->frames++;
            if(parser->handler != NULL)
                parser->handler(&frame, parser->handler_arg);
            parser_discard(parser, frame_length);
        }
    }
}

uint32_t usb_vendor_frame_encode(uint8_t *output,
                                 uint32_t capacity,
                                 uint8_t channel,
                                 uint8_t flags,
                                 uint16_t sequence,
                                 const uint8_t *payload,
                                 uint32_t payload_length)
{
    uint32_t total = USB_VENDOR_HEADER_SIZE + payload_length;

    if(output == NULL || payload_length > USB_VENDOR_MAX_PAYLOAD || total > capacity ||
       (payload == NULL && payload_length != 0U))
        return 0U;

    memcpy(output, g_vendor_magic, sizeof(g_vendor_magic));
    output[4] = channel;
    output[5] = flags;
    put_u16_le(&output[6], sequence);
    put_u32_le(&output[8], payload_length);
    put_u32_le(&output[12], usb_vendor_crc32(payload, payload_length));
    if(payload_length != 0U)
        memcpy(&output[USB_VENDOR_HEADER_SIZE], payload, payload_length);
    return total;
}
