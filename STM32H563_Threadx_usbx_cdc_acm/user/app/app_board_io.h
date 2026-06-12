#ifndef APP_BOARD_IO_H
#define APP_BOARD_IO_H

#include <stdint.h>

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"

UINT app_board_io_init(void);
void app_usb_cdc_activate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);
void app_usb_cdc_deactivate(UX_SLAVE_CLASS_CDC_ACM *cdc_acm);
UX_SLAVE_CLASS_CDC_ACM *app_usb_cdc_get(void);
void app_usb_cdc_process_rx(const uint8_t *data, uint32_t len);
UINT app_usb_cdc_write(const uint8_t *data, uint32_t len);
void app_led_task_entry(ULONG thread_input);
void app_rs485_task_entry(ULONG thread_input);
void app_w800_task_entry(ULONG thread_input);

#endif
