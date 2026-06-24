#include "app_board_io.h"

#include <stdio.h>

#include "app_config.h"
#include "app_nearlink.h"
#include "app_rs485.h"
#include "app_usb_service.h"
#include "app_w800.h"
#include "bsp.h"

static uint8_t g_initialized;

UINT app_board_io_init(void)
{
    if(g_initialized != 0U)
        return TX_SUCCESS;

    bsp_init();
    if(app_usb_service_init() != TX_SUCCESS ||
       app_rs485_init() != TX_SUCCESS ||
       app_w800_init() != TX_SUCCESS ||
       app_nearlink_init() != TX_SUCCESS)
        return TX_START_ERROR;

    g_initialized = 1U;
    return TX_SUCCESS;
}

void app_board_get_status(app_board_status_t *status)
{
    app_usb_service_status_t usb;
    app_w800_status_t w800;

    if(!status)
        return;

    app_usb_service_get_status(&usb);
    app_w800_get_status(&w800);
    status->wifi_ready = w800.wifi_ready;
    status->mqtt_online = w800.mqtt_online;
    status->ota_active = usb.ota_active;
    status->w800_socket_id = w800.socket_id;
    status->ota_received = usb.ota_received;
    status->ota_expected = usb.ota_expected;
    status->vendor_connected = usb.vendor_connected;
    status->vendor_frames = usb.vendor_frames;
    status->vendor_crc_errors = usb.vendor_crc_errors;
    status->vendor_length_errors = usb.vendor_length_errors;
    status->vendor_discarded_bytes = usb.vendor_discarded_bytes;
    status->usb_ldc = usb.ldc;
    status->w800_ldc = w800.ldc;
    app_rs485_get_stats(&status->modbus);
    (void)app_rs485_get_ldc_stats(&status->rs485_ldc);
}

void app_board_request_mqtt_reconnect(void)
{
    app_w800_request_reconnect();
}

void app_led_task_entry(ULONG thread_input)
{
    (void)thread_input;
    for(;;)
    {
        bsp_led_toggle(BSP_LED_STATUS);
        tx_thread_sleep(APP_LED_TOGGLE_TICKS);
    }
}
