#ifndef APP_UI_MODEL_H
#define APP_UI_MODEL_H

#include <stdint.h>

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    int w800_socket_id;
    uint32_t w800_rx_bytes;
    uint32_t w800_packets;

    uint8_t nearlink_active;
    uint8_t nearlink_connected;
    uint8_t nearlink_pending;
    uint8_t nearlink_is_server;
    uint32_t nearlink_rx_bytes;
    uint32_t nearlink_packets;
    char nearlink_local_name[32];
    char nearlink_peer_name[32];

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
} app_ui_model_snapshot_t;

void app_ui_model_get_snapshot(app_ui_model_snapshot_t *snapshot);

#endif /* APP_UI_MODEL_H */
