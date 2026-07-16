/**
 * @file app_ui_model.c
 * @brief Application-to-LVGL diagnostics snapshot adapter.
 */

#include "app_ui_model.h"

#include <string.h>

#include "app_board_io.h"
#include "app_debug.h"
#include "app_rs485.h"
#include "bsp_pwm.h"
#include "bsp_rtc.h"

/** @brief Gather board, communication, and CAN self-test diagnostics. */
void app_ui_model_get_snapshot(app_ui_model_snapshot_t *snapshot)
{
    app_board_status_t board;
    app_rs485_loopback_snapshot_t loopback;
    bsp_pwm_result_t backlight;
    bsp_rtc_datetime_t rtc;
    ldc_stats_t console_stats;

    if(snapshot == NULL)
    {
        return;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    app_board_get_status(&board);


    snapshot->wifi_ready = board.wifi_ready;
    snapshot->mqtt_online = board.mqtt_online;
    snapshot->wifi_provisioning_active = board.wifi_provisioning_active;
    snapshot->wifi_provision_attempts = board.wifi_provision_attempts;
    snapshot->wifi_provision_timeouts = board.wifi_provision_timeouts;
    snapshot->wifi_usb_rescue_state = board.wifi_usb_rescue_state;
    snapshot->wifi_usb_rescue_attempts = board.wifi_usb_rescue_attempts;
    snapshot->w800_socket_id = board.w800_socket_id;
    snapshot->w800_state = board.w800_state;
    snapshot->w800_mqtt_stage = board.w800_mqtt_stage;
    snapshot->w800_socket_local_port = board.w800_socket_local_port;
    snapshot->w800_socket_rx_data = board.w800_socket_rx_data;
    snapshot->w800_socket_recv_fail_count = board.w800_socket_recv_fail_count;
    snapshot->w800_rx_bytes = (uint32_t)board.w800_ldc.rx_bytes;
    snapshot->w800_packets = (uint32_t)board.w800_ldc.packets;
    app_w800_get_scan_snapshot(&snapshot->wifi_scan);

    if(app_debug_get_stats(&console_stats))
    {
        snapshot->console_rx_bytes = (uint32_t)console_stats.rx_bytes;
        snapshot->console_packets = (uint32_t)console_stats.packets;
        snapshot->console_drops = (uint32_t)console_stats.drop;
    }

    snapshot->rs485_rx_frames = board.modbus.rx_frames;
    snapshot->rs485_tx_frames = board.modbus.tx_frames;
    snapshot->rs485_ignored_frames = board.modbus.ignored_frames;
    snapshot->rs485_crc_errors = board.modbus.crc_errors;
    snapshot->rs485_exceptions = board.modbus.exceptions;
    snapshot->rs485_transport_errors = board.modbus.transport_errors;
    snapshot->rs485_rx_bytes = (uint32_t)board.rs485_ldc.rx_bytes;
    snapshot->rs485_packets = (uint32_t)board.rs485_ldc.packets;

    snapshot->usb_connected = board.vendor_connected;
    snapshot->usb_frames = board.vendor_frames;
    snapshot->usb_crc_errors = board.vendor_crc_errors;
    snapshot->usb_length_errors = board.vendor_length_errors;
    snapshot->usb_discarded_bytes = board.vendor_discarded_bytes;
    snapshot->usb_rx_bytes = (uint32_t)board.usb_ldc.rx_bytes;
    snapshot->usb_packets = (uint32_t)board.usb_ldc.packets;

    app_rs485_get_loopback_snapshot(&loopback);
    snapshot->modbus_server_requests = loopback.server_requests;
    snapshot->modbus_server_responses = loopback.server_responses;
    snapshot->modbus_master_passes = loopback.master_passes;
    snapshot->modbus_master_failures = loopback.master_failures;
    snapshot->modbus_last_register_0 = loopback.last_register_0;
    snapshot->modbus_last_register_1 = loopback.last_register_1;
    snapshot->modbus_unit_id = app_rs485_unit_id();

    if(bsp_rtc_get_datetime(&rtc) == BSP_STATUS_OK)
    {
        snapshot->rtc_valid = 1U;
        snapshot->rtc_year = rtc.year;
        snapshot->rtc_month = rtc.month;
        snapshot->rtc_day = rtc.day;
        snapshot->rtc_hour = rtc.hour;
        snapshot->rtc_minute = rtc.minute;
        snapshot->rtc_second = rtc.second;
    }

    if(bsp_pwm_get_result(BOARD_PWM_LCD_BACKLIGHT, &backlight) == BSP_STATUS_OK)
    {
        snapshot->brightness_permille = backlight.requested_duty_permille;
    }

    snapshot->ota_active = board.ota_active;
    snapshot->ota_received = board.ota_received;
    snapshot->ota_expected = board.ota_expected;
    app_can_self_test_get_snapshot(&snapshot->can_self_test);
    app_self_test_get_snapshot(&snapshot->board_self_test);
}
