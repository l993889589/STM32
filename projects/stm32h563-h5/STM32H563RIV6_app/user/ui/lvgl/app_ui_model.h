/**
 * @file app_ui_model.h
 * @brief Coherent application diagnostics exposed to the LVGL presentation.
 */

#ifndef APP_UI_MODEL_H
#define APP_UI_MODEL_H

#include <stdint.h>

#include "app_can_self_test.h"
#include "app_self_test.h"

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    int w800_socket_id;
    uint32_t w800_rx_bytes;
    uint32_t w800_packets;

    uint32_t console_rx_bytes;
    uint32_t console_packets;
    uint32_t console_drops;

    uint32_t rs485_rx_frames;
    uint32_t rs485_tx_frames;
    uint32_t rs485_crc_errors;
    uint32_t rs485_rx_bytes;
    uint32_t rs485_packets;

    uint8_t usb_connected;
    uint32_t usb_frames;
    uint32_t usb_crc_errors;
    uint32_t usb_rx_bytes;
    uint32_t usb_packets;

    app_can_self_test_snapshot_t can_self_test;
    app_self_test_snapshot_t board_self_test;
} app_ui_model_snapshot_t;

/** @brief Copy one application diagnostics snapshot for the UI thread. */
void app_ui_model_get_snapshot(app_ui_model_snapshot_t *snapshot);

#endif /* APP_UI_MODEL_H */
