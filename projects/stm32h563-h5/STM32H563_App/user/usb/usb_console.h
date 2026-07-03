#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"

UINT usb_console_init(void);
void usb_console_activate(UX_SLAVE_CLASS_CDC_ACM *instance);
void usb_console_deactivate(UX_SLAVE_CLASS_CDC_ACM *instance);
UX_SLAVE_CLASS_CDC_ACM *usb_console_instance(void);
bool usb_console_is_connected(void);
UINT usb_console_write(const uint8_t *data, uint32_t length);

#endif /* USB_CONSOLE_H */
