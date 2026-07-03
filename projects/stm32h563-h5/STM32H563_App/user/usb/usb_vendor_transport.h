#ifndef USB_VENDOR_TRANSPORT_H
#define USB_VENDOR_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_dpump.h"
#include "usb_vendor_protocol.h"

UINT usb_vendor_transport_init(usb_vendor_frame_fn handler, void *handler_arg);
void usb_vendor_transport_activate(void *instance);
void usb_vendor_transport_deactivate(void *instance);
bool usb_vendor_transport_is_connected(void);
UINT usb_vendor_transport_read(uint8_t *data, uint32_t capacity, uint32_t *actual);
void usb_vendor_transport_feed(const uint8_t *data, uint32_t length);
UINT usb_vendor_transport_send(uint8_t channel,
                               uint8_t flags,
                               uint16_t sequence,
                               const uint8_t *payload,
                               uint32_t payload_length);
void usb_vendor_transport_get_parser_stats(uint32_t *frames,
                                           uint32_t *crc_errors,
                                           uint32_t *length_errors,
                                           uint32_t *discarded_bytes);

#endif /* USB_VENDOR_TRANSPORT_H */
