#ifndef BOOT_SHELL_H
#define BOOT_SHELL_H

#include <stdint.h>

#include "ota_boot.h"
#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"

UINT boot_shell_init(void);
void boot_shell_set_boot_result(ota_boot_result_t result);
void boot_shell_usb_activate(UX_SLAVE_CLASS_CDC_ACM *instance);
void boot_shell_usb_deactivate(UX_SLAVE_CLASS_CDC_ACM *instance);
void boot_shell_usb_parameter_change(UX_SLAVE_CLASS_CDC_ACM *instance);
UX_SLAVE_CLASS_CDC_ACM *boot_shell_usb_instance(void);
void boot_shell_usb_process(const uint8_t *data, uint32_t length);
void boot_shell_service(void);
void boot_shell_led_task(ULONG input);

#endif /* BOOT_SHELL_H */
