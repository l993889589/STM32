/**
 * @file bsp_usb.h
 * @brief Opaque USB FS device-controller ownership.
 */

#ifndef BSP_USB_H
#define BSP_USB_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Initialize PA11/PA12 and the USB OTG FS peripheral. */
bsp_status_t bsp_usb_init(void);

/** @brief Return an opaque DCD context owned by the BSP. */
uintptr_t bsp_usb_get_dcd_context(void);

/** @brief Configure endpoint FIFOs and connect the USB device. */
bsp_status_t bsp_usb_start(void);

#endif /* BSP_USB_H */
