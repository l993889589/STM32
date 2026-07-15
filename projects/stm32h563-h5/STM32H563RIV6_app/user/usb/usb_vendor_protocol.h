#ifndef USB_VENDOR_PROTOCOL_H
#define USB_VENDOR_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define USB_VENDOR_HEADER_SIZE       16U
#define USB_VENDOR_MAX_PAYLOAD     1024U
#define USB_VENDOR_MAX_FRAME       (USB_VENDOR_HEADER_SIZE + USB_VENDOR_MAX_PAYLOAD)

#define USB_VENDOR_FLAG_RESPONSE   0x01U
#define USB_VENDOR_FLAG_ERROR      0x02U

typedef enum
{
    USB_VENDOR_CHANNEL_CONTROL = 0,
    USB_VENDOR_CHANNEL_LDC = 1,
    USB_VENDOR_CHANNEL_OTA = 2,
    USB_VENDOR_CHANNEL_STRESS = 3,
    USB_VENDOR_CHANNEL_LOG = 4
} usb_vendor_channel_t;

typedef struct
{
    uint8_t channel;
    uint8_t flags;
    uint16_t sequence;
    const uint8_t *payload;
    uint32_t payload_length;
} usb_vendor_frame_t;

typedef void (*usb_vendor_frame_fn)(const usb_vendor_frame_t *frame, void *arg);

typedef struct
{
    uint8_t buffer[USB_VENDOR_MAX_FRAME];
    uint32_t length;
    uint32_t frames;
    uint32_t crc_errors;
    uint32_t length_errors;
    uint32_t discarded_bytes;
    usb_vendor_frame_fn handler;
    void *handler_arg;
} usb_vendor_parser_t;

void usb_vendor_parser_init(usb_vendor_parser_t *parser,
                            usb_vendor_frame_fn handler,
                            void *handler_arg);
void usb_vendor_parser_feed(usb_vendor_parser_t *parser,
                            const uint8_t *data,
                            uint32_t length);
uint32_t usb_vendor_frame_encode(uint8_t *output,
                                 uint32_t capacity,
                                 uint8_t channel,
                                 uint8_t flags,
                                 uint16_t sequence,
                                 const uint8_t *payload,
                                 uint32_t payload_length);
uint32_t usb_vendor_crc32(const uint8_t *data, uint32_t length);

#endif /* USB_VENDOR_PROTOCOL_H */
