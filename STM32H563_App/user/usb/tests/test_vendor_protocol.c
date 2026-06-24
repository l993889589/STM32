#include "usb_vendor_protocol.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static uint32_t calls;
static uint16_t last_sequence;
static uint8_t last_channel;
static uint8_t last_payload[32];
static uint32_t last_length;

static void on_frame(const usb_vendor_frame_t *frame, void *arg)
{
    (void)arg;
    calls++;
    last_sequence = frame->sequence;
    last_channel = frame->channel;
    last_length = frame->payload_length;
    memcpy(last_payload, frame->payload, frame->payload_length);
}

int main(void)
{
    usb_vendor_parser_t parser;
    uint8_t frame[USB_VENDOR_MAX_FRAME];
    uint8_t second[USB_VENDOR_MAX_FRAME];
    const uint8_t payload[] = {0U, 1U, 2U, 3U, 4U, 5U};
    uint32_t length;
    uint32_t second_length;

    usb_vendor_parser_init(&parser, on_frame, NULL);
    length = usb_vendor_frame_encode(frame, sizeof(frame), USB_VENDOR_CHANNEL_STRESS,
                                     0U, 0x1234U, payload, sizeof(payload));
    assert(length == USB_VENDOR_HEADER_SIZE + sizeof(payload));

    usb_vendor_parser_feed(&parser, frame, 3U);
    usb_vendor_parser_feed(&parser, &frame[3], 7U);
    usb_vendor_parser_feed(&parser, &frame[10], length - 10U);
    assert(calls == 1U);
    assert(last_channel == USB_VENDOR_CHANNEL_STRESS);
    assert(last_sequence == 0x1234U);
    assert(last_length == sizeof(payload));
    assert(memcmp(last_payload, payload, sizeof(payload)) == 0);

    second_length = usb_vendor_frame_encode(second, sizeof(second), USB_VENDOR_CHANNEL_CONTROL,
                                            0U, 2U, payload, 2U);
    memcpy(&frame[length], second, second_length);
    usb_vendor_parser_feed(&parser, frame, length + second_length);
    assert(calls == 3U);

    frame[12] ^= 1U;
    usb_vendor_parser_feed(&parser, frame, length);
    assert(calls == 3U);
    assert(parser.crc_errors == 1U);

    usb_vendor_parser_feed(&parser, (const uint8_t *)"noise", 5U);
    usb_vendor_parser_feed(&parser, second, second_length);
    assert(calls == 4U);
    assert(parser.discarded_bytes >= 5U);
    return 0;
}
