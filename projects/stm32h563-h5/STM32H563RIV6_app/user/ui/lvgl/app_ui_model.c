/**
 * @file app_ui_model.c
 * @brief Application-to-LVGL diagnostics snapshot adapter.
 */

#include "app_ui_model.h"

#include <string.h>

#include "app_board_io.h"
#include "app_debug.h"

/** @brief Gather board, communication, and CAN self-test diagnostics. */
void app_ui_model_get_snapshot(app_ui_model_snapshot_t *snapshot)
{
    app_board_status_t board;
    ldc_stats_t console_stats;

    if(snapshot == NULL)
    {
        return;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    app_board_get_status(&board);


    snapshot->wifi_ready = board.wifi_ready;
    snapshot->mqtt_online = board.mqtt_online;
    snapshot->w800_socket_id = board.w800_socket_id;
    snapshot->w800_rx_bytes = (uint32_t)board.w800_ldc.rx_bytes;
    snapshot->w800_packets = (uint32_t)board.w800_ldc.packets;

    if(app_debug_get_stats(&console_stats))
    {
        snapshot->console_rx_bytes = (uint32_t)console_stats.rx_bytes;
        snapshot->console_packets = (uint32_t)console_stats.packets;
        snapshot->console_drops = (uint32_t)console_stats.drop;
    }

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
    app_can_self_test_get_snapshot(&snapshot->can_self_test);
    app_self_test_get_snapshot(&snapshot->board_self_test);
}
