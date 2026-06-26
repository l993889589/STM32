#include "app_ui_model.h"

#include <string.h>

#include "app_board_io.h"
#include "app_nearlink.h"
#include "at_module_nearlink.h"

static void app_ui_model_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if(dst == NULL || dst_size == 0U)
    {
        return;
    }

    if(src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    (void)strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

void app_ui_model_get_snapshot(app_ui_model_snapshot_t *snapshot)
{
    app_board_status_t board;
    app_nearlink_status_t nearlink;

    if(snapshot == NULL)
    {
        return;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    app_board_get_status(&board);
    app_nearlink_get_status(&nearlink);

    snapshot->wifi_ready = board.wifi_ready;
    snapshot->mqtt_online = board.mqtt_online;
    snapshot->w800_socket_id = board.w800_socket_id;
    snapshot->w800_rx_bytes = (uint32_t)board.w800_ldc.rx_bytes;
    snapshot->w800_packets = (uint32_t)board.w800_ldc.packets;

    snapshot->nearlink_active = nearlink.active;
    snapshot->nearlink_connected = nearlink.connected;
    snapshot->nearlink_pending = nearlink.apply_pending;
    snapshot->nearlink_is_server = (nearlink.role == AT_NEARLINK_ROLE_SERVER) ? 1U : 0U;
    snapshot->nearlink_rx_bytes = nearlink.ldc_rx_bytes;
    snapshot->nearlink_packets = nearlink.ldc_packets;
    app_ui_model_copy_text(snapshot->nearlink_local_name,
                           sizeof(snapshot->nearlink_local_name),
                           nearlink.local_name);
    app_ui_model_copy_text(snapshot->nearlink_peer_name,
                           sizeof(snapshot->nearlink_peer_name),
                           nearlink.peer_name);

    snapshot->rs485_rx_frames = board.modbus.rx_frames;
    snapshot->rs485_tx_frames = board.modbus.tx_frames;
    snapshot->rs485_crc_errors = board.modbus.crc_errors;
    snapshot->rs485_rx_bytes = (uint32_t)board.rs485_ldc.rx_bytes;
    snapshot->rs485_packets = (uint32_t)board.rs485_ldc.packets;

    snapshot->usb_connected = board.vendor_connected;
    snapshot->usb_frames = board.vendor_frames;
    snapshot->usb_crc_errors = board.vendor_crc_errors;
    snapshot->usb_rx_bytes = (uint32_t)board.usb_ldc.rx_bytes;
    snapshot->usb_packets = (uint32_t)board.usb_ldc.packets;
}
