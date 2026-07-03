#ifndef APP_BOARD_IO_H
#define APP_BOARD_IO_H

#include <stdint.h>

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"

#define APP_USB_LDC_MAX_FRAME              256U
#define APP_RS485_LDC_MAX_FRAME            256U
#define APP_RS485_MODBUS_UNIT_ID           1U
#define APP_RS485_MODBUS_HOLDING_COUNT     64U
#define APP_RS485_UART_BAUDRATE            115200U
#define APP_W800_UART_BAUDRATE             115200U
#define APP_W800_WIFI_SSID                 "CU_eaJU"
#define APP_W800_WIFI_PASSWORD             "cgzrte4s"
#define APP_W800_MQTT_HOST                 "192.168.1.4"
#define APP_W800_MQTT_PORT                 1883U

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
