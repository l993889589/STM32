/**
 * @file app_shell.c
 * @brief Multi-transport diagnostic shell and application command handlers.
 */

#include "app_shell.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_board_io.h"
#include "app_blackbox.h"
#include "app_can_self_test.h"
#include "app_config.h"
#include "app_firmware_update_service.h"
#include "app_health.h"
#include "app_log.h"
#include "app_power.h"
#include "app_production_test.h"
#include "app_rs485.h"
#include "app_self_test.h"
#include "app_ui.h"
#include "app_w800.h"
#include "bsp.h"
#include "ldc_easy.h"
#include "ldc_port_irq.h"
#include "main.h"
#include "bsp_flash.h"
#include "ota_boot_control.h"
#include "ota_layout.h"
#include "shell.h"
#include "tx_api.h"
#include "ui_asset_store.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

#define APP_SHELL_DEFAULT_TRANSPORT APP_SHELL_TRANSPORT_USB_CDC
#define APP_CONSOLE_LDC_MAX_FRAME   256U
#define APP_CONSOLE_TIMEOUT_MS      20U
#define APP_SHELL_PACKET_COUNT      8U
/* Detailed OTA diagnostics and formatted output peak above the old 2 KiB stack. */
#define APP_SHELL_THREAD_STACK      6144U
#define APP_SHELL_TX_WAIT_TICKS     100U

static shell_t g_app_shell;
static ldc_easy_t g_app_shell_ldc;
static uint8_t g_app_shell_ring[LDC_EASY_RING_BYTES(APP_CONSOLE_LDC_MAX_FRAME, APP_SHELL_PACKET_COUNT)];
static ldc_packet_t g_app_shell_packets[APP_SHELL_PACKET_COUNT];
static uint8_t g_app_shell_frame[APP_CONSOLE_LDC_MAX_FRAME];
static app_log_record_t g_app_shell_log_records[8];
static blackbox_store_record_t g_app_shell_blackbox_records[32];
static char g_app_shell_blackbox_json[384];
static char g_app_shell_production_json[384];
static TX_THREAD g_app_shell_thread;
static TX_SEMAPHORE g_app_shell_sem;
static UCHAR g_app_shell_thread_stack[APP_SHELL_THREAD_STACK];
static app_shell_write_fn g_transport_write[APP_SHELL_TRANSPORT_COUNT];
static void *g_transport_arg[APP_SHELL_TRANSPORT_COUNT];
static uint8_t g_transport_connected[APP_SHELL_TRANSPORT_COUNT];
static app_shell_transport_t g_active_transport = APP_SHELL_TRANSPORT_NONE;
static uint8_t g_app_shell_banner_pending;
static uint8_t g_app_shell_initialized;
static char g_wifi_rescue_ssid[33];

static const char g_app_shell_banner[] =
    "\r\n\033[1;36m"
    "====================================================\r\n"
    "   _      _____  _____  _   _   ___   \r\n"
    "  | |    | ____|| ____|| \\ | | / _ \\  \r\n"
    "  | |    |  _|  |  _|  |  \\| || | | | \r\n"
    "  | |___ | |___ | |___ | |\\  || |_| | \r\n"
    "  |_____| |_____||_____||_| \\_| \\___/  \r\n"
    "                                                    \r\n"
    "        Industrial Embedded System (LEDUO)          \r\n"
    "====================================================\r\n"
    "\033[0m"
    "\033[1;33m"
    " MCU     : STM32H563\r\n"
    " FW      : LeduO Firmware " APP_FIRMWARE_VERSION "\r\n"
    " ARCH    : BSP / LDC / AT / RTOS\r\n"
    " MODE    : DMA + Event Driven\r\n"
    "\033[0m"
    "\033[1;32m"
    " STATUS  : System Initializing...\r\n"
    "\033[0m"
    "\r\nType 'help' for CLI commands.\r\n"
    "====================================================\r\n\r\n";

static int app_shell_write(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    if(g_active_transport <= APP_SHELL_TRANSPORT_NONE ||
       g_active_transport >= APP_SHELL_TRANSPORT_COUNT ||
       g_transport_write[g_active_transport] == NULL)
    {
        return -1;
    }
    return g_transport_write[g_active_transport](data, length, g_transport_arg[g_active_transport]);
}

static int app_shell_log_sink(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;

    if(shell_has_partial_line(&g_app_shell))
        return -1;
    return app_shell_write(data, length, NULL);
}

static void app_shell_wake(void)
{
    if(g_app_shell_initialized != 0U)
        (void)tx_semaphore_put(&g_app_shell_sem);
}

static void app_shell_ldc_event(ldc_easy_t *queue, ldc_easy_event_t event, void *arg)
{
    (void)queue;
    (void)arg;

    if(event == LDC_EASY_EVT_PACKET)
        app_shell_wake();
}

static int app_shell_cmd_help(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)arg;
    shell_show_help(shell, (argc > 1) ? argv[1] : NULL);
    return 0;
}

static int app_shell_cmd_version(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    (void)shell_printf(shell, "firmware %s, bsp %s\r\n", APP_FIRMWARE_VERSION, BSP_VERSION);
    return 0;
}

static int app_shell_cmd_uptime(shell_t *shell, int argc, char **argv, void *arg)
{
    uint64_t milliseconds;

    (void)argc;
    (void)argv;
    (void)arg;
    milliseconds = ((uint64_t)tx_time_get() * 1000ULL) / TX_TIMER_TICKS_PER_SECOND;
    (void)shell_printf(shell, "uptime %llu ms\r\n", (unsigned long long)milliseconds);
    return 0;
}

static int app_shell_cmd_reboot(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)arg;
    if(argc != 2 || strcmp(argv[1], "now") != 0)
    {
        (void)shell_write(shell, "usage: reboot now\r\n");
        return -1;
    }
    (void)shell_write(shell, "rebooting...\r\n");
    tx_thread_sleep(20U);
    bsp_system_reset();
    return 0;
}

static int app_shell_cmd_clear(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    (void)shell_write(shell, "\033[2J\033[H");
    return 0;
}

static void app_shell_print_ldc(shell_t *shell,
                                const char *name,
                                const ldc_stats_t *stats)
{
    if(!shell || !name || !stats)
        return;

    (void)shell_printf(shell,
                       "%s ldc rx=%llu packets=%llu overflow=%llu drop=%llu peak=%llu queue_peak=%llu\r\n",
                       name,
                       (unsigned long long)stats->rx_bytes,
                       (unsigned long long)stats->packets,
                       (unsigned long long)stats->overflow,
                       (unsigned long long)stats->drop,
                       (unsigned long long)stats->max_used,
                       (unsigned long long)stats->packet_peak);
}

static int app_shell_cmd_modbus(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;
    app_rs485_loopback_snapshot_t loopback;
    char payload[256];
    char command[96];

    (void)arg;
    if(argc == 3 && strcmp(argv[1], "ports") == 0)
    {
        (void)snprintf(command, sizeof(command), "ports=%s", argv[2]);
        if(app_rs485_apply_network_command(command))
        {
            (void)shell_write(shell, "modbus ports updated\r\n");
            return 0;
        }
        (void)shell_write(shell, "modbus ports rejected\r\n");
        return -1;
    }

    if(argc == 8 && strcmp(argv[1], "poll") == 0)
    {
        (void)snprintf(command, sizeof(command),
                       "poll=%s,%s,%s,%s,%s,%s",
                       argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
        if(app_rs485_apply_network_command(command))
        {
            (void)shell_write(shell, "modbus poll updated\r\n");
            return 0;
        }
        (void)shell_write(shell, "modbus poll rejected\r\n");
        return -1;
    }

    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: modbus status|ports 2|4|both|off|poll port slave fc addr count period\r\n");
        return -1;
    }

    app_board_get_status(&status);
    app_rs485_get_loopback_snapshot(&loopback);
    (void)shell_printf(shell,
                       "modbus unit=%u rx=%lu tx=%lu ignored=%lu crc=%lu exceptions=%lu io_errors=%lu\r\n",
                       (unsigned int)app_rs485_unit_id(),
                       (unsigned long)status.modbus.rx_frames,
                       (unsigned long)status.modbus.tx_frames,
                       (unsigned long)status.modbus.ignored_frames,
                       (unsigned long)status.modbus.crc_errors,
                       (unsigned long)status.modbus.exceptions,
                       (unsigned long)status.modbus.transport_errors);
    app_shell_print_ldc(shell, "rs485", &status.rs485_ldc);
    (void)shell_printf(shell,
                       "rs485 loopback request=%lu response=%lu pass=%lu fail=%lu crc=%lu protocol=%lu registers=%u,%u\r\n",
                       (unsigned long)loopback.server_requests,
                       (unsigned long)loopback.server_responses,
                       (unsigned long)loopback.master_passes,
                       (unsigned long)loopback.master_failures,
                       (unsigned long)loopback.server_crc_errors,
                       (unsigned long)loopback.server_protocol_errors,
                       loopback.last_register_0,
                       loopback.last_register_1);
    if(app_rs485_format_network_payload(APP_RS485_NET_STATUS, payload, sizeof(payload)) > 0)
        (void)shell_printf(shell, "modbus status-json %s\r\n", payload);
    if(app_rs485_format_network_payload(APP_RS485_NET_DATA, payload, sizeof(payload)) > 0)
        (void)shell_printf(shell, "modbus data-json %s\r\n", payload);
    return 0;
}

static void app_shell_secure_zero(void *data, size_t length)
{
    volatile uint8_t *cursor = (volatile uint8_t *)data;

    while(length-- != 0U)
        *cursor++ = 0U;
}

static const char *app_shell_usb_rescue_state(uint8_t state)
{
    switch((app_w800_usb_rescue_state_t)state)
    {
    case APP_W800_USB_RESCUE_IDLE: return "idle";
    case APP_W800_USB_RESCUE_PENDING: return "pending";
    case APP_W800_USB_RESCUE_APPLYING: return "applying";
    case APP_W800_USB_RESCUE_SAVED: return "saved";
    case APP_W800_USB_RESCUE_CONNECTED: return "connected";
    case APP_W800_USB_RESCUE_FAILED: return "failed";
    default: return "unknown";
    }
}

static bool app_shell_wifi_ssid_is_valid(const char *ssid, uint16_t length)
{
    uint16_t i;

    if(ssid == NULL || length == 0U || length > 32U)
        return false;
    for(i = 0U; i < length; i++)
    {
        const unsigned char ch = (unsigned char)ssid[i];

        if(ch < 0x20U || ch > 0x7EU || ch == (unsigned char)'"')
            return false;
    }
    return true;
}

static void app_shell_wifi_password_input(shell_t *shell,
                                          const char *password,
                                          uint16_t length,
                                          void *arg)
{
    app_w800_credentials_result_t result;

    (void)arg;
    if(password == NULL)
    {
        app_shell_secure_zero(g_wifi_rescue_ssid, sizeof(g_wifi_rescue_ssid));
        (void)shell_write(shell, "USB rescue cancelled\r\n");
        return;
    }

    if(length < 8U || length > 63U)
    {
        app_shell_secure_zero(g_wifi_rescue_ssid, sizeof(g_wifi_rescue_ssid));
        (void)shell_write(shell, "password must be 8..63 printable ASCII bytes\r\n");
        return;
    }

    result = app_w800_request_usb_credentials(g_wifi_rescue_ssid, password);
    app_shell_secure_zero(g_wifi_rescue_ssid, sizeof(g_wifi_rescue_ssid));
    switch(result)
    {
    case APP_W800_CREDENTIALS_ACCEPTED:
        (void)shell_write(shell,
                          "credentials queued; use 'wifi status' for the result\r\n");
        break;
    case APP_W800_CREDENTIALS_INVALID_SSID:
        (void)shell_write(shell, "invalid SSID\r\n");
        break;
    case APP_W800_CREDENTIALS_INVALID_PASSWORD:
        (void)shell_write(shell, "invalid password\r\n");
        break;
    case APP_W800_CREDENTIALS_BUSY:
        (void)shell_write(shell, "USB rescue is already pending\r\n");
        break;
    default:
        (void)shell_write(shell, "W800 service is not ready\r\n");
        break;
    }
}

static void app_shell_wifi_ssid_input(shell_t *shell,
                                      const char *ssid,
                                      uint16_t length,
                                      void *arg)
{
    (void)arg;
    if(ssid == NULL)
    {
        app_shell_secure_zero(g_wifi_rescue_ssid, sizeof(g_wifi_rescue_ssid));
        (void)shell_write(shell, "USB rescue cancelled\r\n");
        return;
    }
    if(!app_shell_wifi_ssid_is_valid(ssid, length))
    {
        (void)shell_write(shell,
                          "SSID must be 1..32 printable ASCII bytes without a quote\r\n");
        (void)shell_begin_line_input(shell,
                                     "SSID: ",
                                     true,
                                     app_shell_wifi_ssid_input,
                                     NULL);
        return;
    }

    memcpy(g_wifi_rescue_ssid, ssid, length);
    g_wifi_rescue_ssid[length] = '\0';
    if(!shell_begin_line_input(shell,
                               "Password (hidden): ",
                               false,
                               app_shell_wifi_password_input,
                               NULL))
    {
        app_shell_secure_zero(g_wifi_rescue_ssid, sizeof(g_wifi_rescue_ssid));
        (void)shell_write(shell, "unable to start password capture\r\n");
    }
}

static int app_shell_cmd_wifi(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;

    (void)arg;
    if(argc == 2 &&
       (strcmp(argv[1], "ble") == 0 || strcmp(argv[1], "provision") == 0))
    {
        app_w800_request_ble_provisioning();
        (void)shell_write(shell, "W800 BLE provisioning requested\r\n");
        return 0;
    }
    if(argc == 2 && strcmp(argv[1], "rescue") == 0)
    {
        app_shell_secure_zero(g_wifi_rescue_ssid, sizeof(g_wifi_rescue_ssid));
        if(!shell_begin_line_input(shell,
                                   "SSID: ",
                                   true,
                                   app_shell_wifi_ssid_input,
                                   NULL))
        {
            (void)shell_write(shell, "another input request is active\r\n");
            return -1;
        }
        return 0;
    }
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: wifi status|ble|rescue\r\n");
        return -1;
    }

    app_board_get_status(&status);
    (void)shell_printf(shell,
                       "wifi %s, provisioning=ble active=%u attempts=%lu timeouts=%lu "
                       "usb_rescue=%s rescue_attempts=%lu\r\n",
                       status.wifi_ready ? "ready" : "offline",
                       status.wifi_provisioning_active,
                       (unsigned long)status.wifi_provision_attempts,
                       (unsigned long)status.wifi_provision_timeouts,
                       app_shell_usb_rescue_state(status.wifi_usb_rescue_state),
                       (unsigned long)status.wifi_usb_rescue_attempts);
    app_shell_print_ldc(shell, "w800", &status.w800_ldc);
    return 0;
}

static int app_shell_cmd_mqtt(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;

    (void)arg;
    if(argc == 2 && strcmp(argv[1], "reconnect") == 0)
    {
        app_board_request_mqtt_reconnect();
        (void)shell_write(shell, "mqtt reconnect requested\r\n");
        return 0;
    }
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: mqtt status|reconnect\r\n");
        return -1;
    }

    app_board_get_status(&status);
    (void)shell_printf(shell,
                       "mqtt %s, broker=%s:%u, socket=%d state=%u stage=%u local=%u rx=%lu rres=%u ract=%u rfail=%lu rb=%02X%02X%02X%02X pub=%lu begin=%lu chunk=%lu commit=%lu drops=%lu last=%s/%u http=%u/%u state=%u %lu/%lu err=%s ui_chunk=%u state=%u %lu/%lu unit=%u retry=%u seen=%lu drop=%lu seq=%lu ofs=%lu b64=%lu crc=%lu err=%s\r\n",
                       status.mqtt_online ? "online" : "offline",
                       app_w800_mqtt_host(),
                       (unsigned int)app_w800_mqtt_port(),
                       status.w800_socket_id,
                       status.w800_state,
                       status.w800_mqtt_stage,
                       (unsigned int)status.w800_socket_local_port,
                       (unsigned long)status.w800_socket_rx_data,
                       status.w800_socket_recv_result,
                       (unsigned int)status.w800_socket_recv_actual,
                       (unsigned long)status.w800_socket_recv_fail_count,
                       status.w800_socket_recv_head[0],
                       status.w800_socket_recv_head[1],
                       status.w800_socket_recv_head[2],
                       status.w800_socket_recv_head[3],
                       (unsigned long)status.w800_mqtt_publish_seen,
                       (unsigned long)status.w800_mqtt_begin_seen,
                       (unsigned long)status.w800_mqtt_chunk_seen,
                       (unsigned long)status.w800_mqtt_commit_seen,
                       (unsigned long)status.w800_mqtt_stream_drops,
                       status.w800_mqtt_last_topic,
                       (unsigned int)status.w800_mqtt_last_payload_len,
                       status.w800_http_pending,
                       status.w800_http_active,
                       status.w800_http_state,
                       (unsigned long)status.w800_http_received,
                       (unsigned long)status.w800_http_size,
                       status.w800_http_error,
                       status.w800_chunk_active,
                       status.w800_chunk_state,
                       (unsigned long)status.w800_chunk_received,
                       (unsigned long)status.w800_chunk_size,
                       (unsigned int)status.w800_chunk_unit,
                       status.w800_chunk_retry,
                       (unsigned long)status.w800_chunk_json_seen,
                       (unsigned long)status.w800_chunk_json_drop,
                       (unsigned long)status.w800_chunk_seq_error,
                       (unsigned long)status.w800_chunk_offset_error,
                       (unsigned long)status.w800_chunk_b64_error,
                       (unsigned long)status.w800_chunk_crc_error,
                       status.w800_chunk_error);
    return 0;
}

static uint8_t app_shell_control_read(
    void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    (void)context;
    return bsp_flash_read(address, data, size) ? 1U : 0U;
}

static uint8_t app_shell_control_erase(void *context, uint32_t address)
{
    (void)context;
    return bsp_flash_erase_4k(address) ? 1U : 0U;
}

static uint8_t app_shell_control_write(
    void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    (void)context;
    return bsp_flash_write(address, data, size) ? 1U : 0U;
}

static int app_shell_cmd_ota(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;
    app_health_status_t health;
    ota_boot_control_storage_t control_storage;
    ota_boot_control_record_t control;
    ota_control_copy_t control_copy;
    uint8_t firmware_active;
    uint32_t firmware_slot;
    uint32_t firmware_received;
    uint32_t firmware_expected;

    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: ota status\r\n");
        return -1;
    }

    app_board_get_status(&status);
    (void)shell_printf(shell, "ota %s, received=%lu/%lu bytes\r\n",
                       status.ota_active ? "active" : "idle",
                       (unsigned long)status.ota_received,
                       (unsigned long)status.ota_expected);

    app_firmware_update_service_get_progress(
        &firmware_active, &firmware_slot, &firmware_received, &firmware_expected);
    app_health_get_status(30000U, &health);
    (void)shell_printf(shell,
                       "firmware active=%u slot=%lu received=%lu/%lu\r\n"
                       "health required=0x%lX seen=0x%lX stale=0x%lX fault=%lu ticks=%lu\r\n",
                       firmware_active,
                       (unsigned long)firmware_slot,
                       (unsigned long)firmware_received,
                       (unsigned long)firmware_expected,
                       (unsigned long)health.required_mask,
                       (unsigned long)health.seen_mask,
                       (unsigned long)health.stale_mask,
                       (unsigned long)health.fatal_fault,
                       (unsigned long)health.observation_ticks);

    control_storage.context = NULL;
    control_storage.read = app_shell_control_read;
    control_storage.erase_sector = app_shell_control_erase;
    control_storage.write = app_shell_control_write;
    if(ota_boot_control_storage_load(
           &control_storage, &control, &control_copy) == OTA_CONTROL_STATUS_OK)
    {
        (void)shell_printf(shell,
                           "control copy=%u seq=%lu state=%lu active=%lu pending=%lu trial=%lu/%lu min=%lu error=%lu@0x%08lX\r\n",
                           (unsigned int)control_copy,
                           (unsigned long)control.sequence,
                           (unsigned long)control.state,
                           (unsigned long)control.active_slot,
                           (unsigned long)control.pending_slot,
                           (unsigned long)control.trial_boot_count,
                           (unsigned long)control.trial_boot_limit,
                           (unsigned long)control.minimum_version,
                           (unsigned long)control.last_error,
                           (unsigned long)control.last_error_address);
    }
    else
    {
        (void)shell_write(shell, "control unavailable\r\n");
    }
    return 0;
}

static int app_shell_cmd_usb(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;

    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: usb status\r\n");
        return -1;
    }

    app_board_get_status(&status);
    (void)shell_printf(shell,
                       "vendor %s, frames=%lu crc=%lu length=%lu discarded=%lu\r\n",
                       status.vendor_connected ? "connected" : "offline",
                       (unsigned long)status.vendor_frames,
                       (unsigned long)status.vendor_crc_errors,
                       (unsigned long)status.vendor_length_errors,
                       (unsigned long)status.vendor_discarded_bytes);
    app_shell_print_ldc(shell, "usb", &status.usb_ldc);
    return 0;
}

static uint16_t app_shell_parse_count(const char *text, uint16_t fallback, uint16_t limit)
{
    unsigned long value;

    if(text == NULL)
        return fallback;
    value = strtoul(text, NULL, 0);
    if(value == 0UL)
        return fallback;
    if(value > limit)
        return limit;
    return (uint16_t)value;
}

static void app_shell_print_log_record(shell_t *shell, const app_log_record_t *record)
{
    if(shell == NULL || record == NULL)
        return;

    (void)shell_printf(shell,
                       "[%lu] %-5s %-5s %s\r\n",
                       (unsigned long)record->timestamp_ms,
                       app_log_level_name(record->level),
                       app_log_module_name(record->module),
                       record->message);
}

static void app_shell_print_log_levels(shell_t *shell)
{
    for(uint32_t i = 0U; i < APP_LOG_MODULE_COUNT; i++)
    {
        (void)shell_printf(shell,
                           "%-5s %s\r\n",
                           app_log_module_name((app_log_module_t)i),
                           app_log_level_name(app_log_get_level((app_log_module_t)i)));
    }
}

static int app_shell_cmd_log(shell_t *shell, int argc, char **argv, void *arg)
{
    app_log_stats_t stats;
    uint16_t count;

    (void)arg;

    if(argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0))
    {
        app_log_get_stats(&stats);
        (void)shell_printf(shell,
                           "log records=%u/%u written=%lu dropped=%lu console=%s\r\n",
                           (unsigned int)stats.stored,
                           (unsigned int)APP_LOG_RECORD_COUNT,
                           (unsigned long)stats.written,
                           (unsigned long)stats.dropped,
                           stats.console_enabled ? "on" : "off");
        return 0;
    }

    if((argc == 2 || argc == 3) &&
       (strcmp(argv[1], "show") == 0 || strcmp(argv[1], "tail") == 0))
    {
        uint16_t requested = app_shell_parse_count((argc == 3) ? argv[2] : NULL,
                                                   (uint16_t)(sizeof(g_app_shell_log_records) / sizeof(g_app_shell_log_records[0])),
                                                   (uint16_t)(sizeof(g_app_shell_log_records) / sizeof(g_app_shell_log_records[0])));
        count = app_log_read_tail(g_app_shell_log_records, requested);
        for(uint16_t i = 0U; i < count; i++)
            app_shell_print_log_record(shell, &g_app_shell_log_records[i]);
        return 0;
    }

    if(argc == 2 && strcmp(argv[1], "clear") == 0)
    {
        app_log_clear();
        (void)shell_write(shell, "log cleared\r\n");
        return 0;
    }

    if(argc == 2 && strcmp(argv[1], "level") == 0)
    {
        app_shell_print_log_levels(shell);
        return 0;
    }

    if(argc == 4 && strcmp(argv[1], "level") == 0)
    {
        app_log_module_t module;
        app_log_level_t level;

        if(!app_log_parse_module(argv[2], &module) || !app_log_parse_level(argv[3], &level))
        {
            (void)shell_write(shell, "usage: log level <module> error|warn|info|debug|trace|off\r\n");
            return -1;
        }
        (void)app_log_set_level(module, level);
        (void)shell_printf(shell, "log %s level %s\r\n", app_log_module_name(module), app_log_level_name(level));
        return 0;
    }

    if(argc == 3 && strcmp(argv[1], "console") == 0)
    {
        if(strcmp(argv[2], "on") == 0)
            app_log_set_console_enabled(true);
        else if(strcmp(argv[2], "off") == 0)
            app_log_set_console_enabled(false);
        else
        {
            (void)shell_write(shell, "usage: log console on|off\r\n");
            return -1;
        }
        (void)shell_printf(shell, "log console %s\r\n", app_log_console_enabled() ? "on" : "off");
        return 0;
    }

    (void)shell_write(shell, "usage: log status|show [n]|tail [n]|clear|level [module level]|console on|off\r\n");
    return -1;
}

/** @brief Print one persistent black-box record with an RTC calendar when valid. */
static void app_shell_print_blackbox_record(
    shell_t *shell,
    const blackbox_store_record_t *record)
{
    bsp_rtc_datetime_t datetime;
    char payload[BLACKBOX_STORE_PAYLOAD_SIZE + 1U];

    if((shell == NULL) || (record == NULL))
    {
        return;
    }
    (void)memset(payload, 0, sizeof(payload));
    if(record->event.payload_length > 0U)
    {
        (void)memcpy(payload,
                     record->event.payload,
                     record->event.payload_length);
    }
    if(((record->event.flags & APP_BLACKBOX_FLAG_RTC_VALID) != 0U) &&
       app_blackbox_seconds_to_datetime(record->event.rtc_seconds_2000,
                                        &datetime))
    {
        (void)shell_printf(shell,
                           "#%lu %04u-%02u-%02u %02u:%02u:%02u type=%u level=%u src=%u code=%u up=%lu %s\r\n",
                           (unsigned long)record->sequence,
                           datetime.year,
                           datetime.month,
                           datetime.day,
                           datetime.hour,
                           datetime.minute,
                           datetime.second,
                           record->event.type,
                           record->event.severity,
                           record->event.source,
                           record->event.code,
                           (unsigned long)record->event.uptime_ms,
                           payload);
    }
    else
    {
        (void)shell_printf(shell,
                           "#%lu rtc=invalid type=%u level=%u src=%u code=%u up=%lu %s\r\n",
                           (unsigned long)record->sequence,
                           record->event.type,
                           record->event.severity,
                           record->event.source,
                           record->event.code,
                           (unsigned long)record->event.uptime_ms,
                           payload);
    }
}

/** @brief Convert a bounded binary payload to uppercase hexadecimal text. */
static void app_shell_blackbox_payload_hex(
    const blackbox_store_record_t *record,
    char *text,
    uint32_t text_size)
{
    static const char digits[] = "0123456789ABCDEF";
    uint32_t index;
    uint32_t length;

    if((record == NULL) || (text == NULL) || (text_size == 0U))
    {
        return;
    }
    length = record->event.payload_length;
    if(length > BLACKBOX_STORE_PAYLOAD_SIZE)
    {
        length = BLACKBOX_STORE_PAYLOAD_SIZE;
    }
    if((length * 2U + 1U) > text_size)
    {
        length = (text_size - 1U) / 2U;
    }
    for(index = 0U; index < length; ++index)
    {
        text[index * 2U] = digits[record->event.payload[index] >> 4];
        text[index * 2U + 1U] =
            digits[record->event.payload[index] & 0x0FU];
    }
    text[length * 2U] = '\0';
}

/** @brief Emit one machine-readable black-box record for desktop tools. */
static void app_shell_print_blackbox_json(
    shell_t *shell,
    const blackbox_store_record_t *record)
{
    bsp_rtc_datetime_t datetime;
    char timestamp[24];
    char payload_hex[(BLACKBOX_STORE_PAYLOAD_SIZE * 2U) + 1U];
    bool rtc_valid;

    if((shell == NULL) || (record == NULL))
    {
        return;
    }
    rtc_valid = ((record->event.flags & APP_BLACKBOX_FLAG_RTC_VALID) != 0U) &&
                app_blackbox_seconds_to_datetime(
                    record->event.rtc_seconds_2000,
                    &datetime);
    if(rtc_valid)
    {
        (void)snprintf(timestamp,
                       sizeof(timestamp),
                       "%04u-%02u-%02uT%02u:%02u:%02u",
                       datetime.year,
                       datetime.month,
                       datetime.day,
                       datetime.hour,
                       datetime.minute,
                       datetime.second);
    }
    else
    {
        timestamp[0] = '\0';
    }
    app_shell_blackbox_payload_hex(record,
                                   payload_hex,
                                   (uint32_t)sizeof(payload_hex));
    (void)snprintf(g_app_shell_blackbox_json,
                   sizeof(g_app_shell_blackbox_json),
                   "{\"kind\":\"blackbox\",\"sequence\":%lu,\"rtc_valid\":%s,\"timestamp\":%s%s%s,\"type\":%u,\"severity\":%u,\"source\":%u,\"code\":%u,\"uptime_ms\":%lu,\"flags\":%u,\"payload_length\":%u,\"payload_hex\":\"%s\"}\r\n",
                   (unsigned long)record->sequence,
                   rtc_valid ? "true" : "false",
                   rtc_valid ? "\"" : "",
                   rtc_valid ? timestamp : "null",
                   rtc_valid ? "\"" : "",
                   record->event.type,
                   record->event.severity,
                   record->event.source,
                   record->event.code,
                   (unsigned long)record->event.uptime_ms,
                   record->event.flags,
                   record->event.payload_length,
                   payload_hex);
    (void)shell_write(shell, g_app_shell_blackbox_json);
}

/** @brief Inspect, append to, or logically clear the persistent black box. */
static int app_shell_cmd_blackbox(shell_t *shell,
                                  int argc,
                                  char **argv,
                                  void *arg)
{
    app_blackbox_snapshot_t snapshot;
    uint16_t count;
    uint16_t index;

    (void)arg;
    if((argc == 1) || ((argc == 2) &&
                       (strcmp(argv[1], "status") == 0)))
    {
        app_blackbox_get_snapshot(&snapshot);
        (void)shell_printf(shell,
                           "blackbox ready=%u rtc=%u seeded=%u rtc_status=%d reset=0x%08lX\r\n"
                           "records=%lu/%lu generation=%lu newest=%lu sector=%u slot=%u recovered=%lu corrupt=%lu io_error=%lu\r\n"
                           "queue=%lu dropped=%lu written=%lu failed=%lu region=0x%08lX+0x%08lX\r\n",
                           snapshot.is_initialized,
                           snapshot.rtc_is_valid,
                           snapshot.rtc_was_build_seeded,
                           (int)snapshot.rtc_status,
                           (unsigned long)snapshot.reset_causes,
                           (unsigned long)snapshot.store.stored_records,
                           (unsigned long)snapshot.store.record_capacity,
                           (unsigned long)snapshot.store.generation,
                           (unsigned long)snapshot.store.newest_sequence,
                           snapshot.store.active_sector,
                           snapshot.store.active_slot,
                           (unsigned long)snapshot.store.recovered_records,
                           (unsigned long)snapshot.store.corrupt_records,
                           (unsigned long)snapshot.store.io_errors,
                           (unsigned long)snapshot.queued_events,
                           (unsigned long)snapshot.dropped_events,
                           (unsigned long)snapshot.written_events,
                           (unsigned long)snapshot.failed_events,
                           (unsigned long)BLACKBOX_EXT_FLASH_ADDR,
                           (unsigned long)BLACKBOX_EXT_FLASH_SIZE);
        return snapshot.is_initialized ? 0 : -1;
    }
    if((argc == 2 || argc == 3) && (strcmp(argv[1], "tail") == 0))
    {
        uint16_t requested = app_shell_parse_count(
            (argc == 3) ? argv[2] : NULL,
            (uint16_t)(sizeof(g_app_shell_blackbox_records) /
                       sizeof(g_app_shell_blackbox_records[0])),
            (uint16_t)(sizeof(g_app_shell_blackbox_records) /
                       sizeof(g_app_shell_blackbox_records[0])));

        count = app_blackbox_read_tail(g_app_shell_blackbox_records,
                                       requested);
        for(index = 0U; index < count; ++index)
        {
            app_shell_print_blackbox_record(
                shell,
                &g_app_shell_blackbox_records[index]);
        }
        return 0;
    }
    if((argc == 2 || argc == 3) && (strcmp(argv[1], "export") == 0))
    {
        uint16_t requested = app_shell_parse_count(
            (argc == 3) ? argv[2] : NULL,
            (uint16_t)(sizeof(g_app_shell_blackbox_records) /
                       sizeof(g_app_shell_blackbox_records[0])),
            (uint16_t)(sizeof(g_app_shell_blackbox_records) /
                       sizeof(g_app_shell_blackbox_records[0])));

        count = app_blackbox_read_tail(g_app_shell_blackbox_records,
                                       requested);
        (void)shell_printf(shell,
                           "blackbox_export_begin count=%u\r\n",
                           count);
        for(index = 0U; index < count; ++index)
        {
            app_shell_print_blackbox_json(
                shell,
                &g_app_shell_blackbox_records[index]);
        }
        (void)shell_printf(shell,
                           "blackbox_export_end count=%u\r\n",
                           count);
        return 0;
    }
    if((argc == 3) && (strcmp(argv[1], "mark") == 0))
    {
        uint16_t length = (uint16_t)strlen(argv[2]);

        if(length > BLACKBOX_STORE_PAYLOAD_SIZE)
        {
            length = BLACKBOX_STORE_PAYLOAD_SIZE;
        }
        if(!app_blackbox_record(APP_BLACKBOX_EVENT_MANUAL,
                                APP_BLACKBOX_SEVERITY_INFO,
                                0U,
                                0U,
                                argv[2],
                                length))
        {
            (void)shell_write(shell, "blackbox queue busy\r\n");
            return -1;
        }
        (void)shell_write(shell, "blackbox marker queued\r\n");
        return 0;
    }
    if((argc == 3) && (strcmp(argv[1], "clear") == 0) &&
       (strcmp(argv[2], "confirm") == 0))
    {
        bsp_status_t status = app_blackbox_clear();

        (void)shell_printf(shell, "blackbox clear status=%d\r\n", (int)status);
        return (status == BSP_STATUS_OK) ? 0 : -1;
    }
    (void)shell_write(shell,
                      "usage: blackbox status|tail [n]|export [n]|mark <text>|clear confirm\r\n");
    return -1;
}

/** @brief Inspect or set the retained board RTC calendar. */
static int app_shell_cmd_rtc(shell_t *shell,
                             int argc,
                             char **argv,
                             void *arg)
{
    bsp_rtc_datetime_t datetime;
    bsp_status_t status;

    (void)arg;
    if((argc == 1) || ((argc == 2) && (strcmp(argv[1], "status") == 0)))
    {
        status = app_blackbox_get_datetime(&datetime);
        if(status == BSP_STATUS_OK)
        {
            (void)shell_printf(shell,
                               "rtc %04u-%02u-%02u %02u:%02u:%02u weekday=%u\r\n",
                               datetime.year,
                               datetime.month,
                               datetime.day,
                               datetime.hour,
                               datetime.minute,
                               datetime.second,
                               datetime.weekday);
            return 0;
        }
        (void)shell_printf(shell, "rtc status=%d\r\n", (int)status);
        return -1;
    }
    if((argc == 4) && (strcmp(argv[1], "set") == 0))
    {
        unsigned int year;
        unsigned int month;
        unsigned int day;
        unsigned int hour;
        unsigned int minute;
        unsigned int second;

        if((sscanf(argv[2], "%u-%u-%u", &year, &month, &day) != 3) ||
           (sscanf(argv[3], "%u:%u:%u", &hour, &minute, &second) != 3))
        {
            (void)shell_write(shell,
                              "usage: rtc set YYYY-MM-DD HH:MM:SS\r\n");
            return -1;
        }
        (void)memset(&datetime, 0, sizeof(datetime));
        datetime.year = (uint16_t)year;
        datetime.month = (uint8_t)month;
        datetime.day = (uint8_t)day;
        datetime.hour = (uint8_t)hour;
        datetime.minute = (uint8_t)minute;
        datetime.second = (uint8_t)second;
        datetime.weekday = 0U;
        status = app_blackbox_set_datetime(&datetime);
        (void)shell_printf(shell, "rtc set status=%d\r\n", (int)status);
        return (status == BSP_STATUS_OK) ? 0 : -1;
    }
    (void)shell_write(shell, "usage: rtc status|set YYYY-MM-DD HH:MM:SS\r\n");
    return -1;
}

/** @brief Print or restart the structured whole-board automatic self-test. */
static int app_shell_cmd_self_test(shell_t *shell,
                                   int argc,
                                   char **argv,
                                   void *arg)
{
    app_self_test_snapshot_t snapshot;
    uint32_t index;

    (void)arg;
    if((argc == 2) && (strcmp(argv[1], "run") == 0))
    {
        app_self_test_request_run();
        (void)shell_write(shell, "self_test run requested\r\n");
        return 0;
    }
    if((argc != 1) && !((argc == 2) &&
                        (strcmp(argv[1], "status") == 0)))
    {
        (void)shell_write(shell, "usage: self_test status|run\r\n");
        return -1;
    }

    app_self_test_get_snapshot(&snapshot);
    (void)shell_printf(shell,
                       "self_test state=%u generation=%lu pass=%u fail=%u not_connected=%u not_installed=%u duration=%lu ms\r\n",
                       snapshot.state,
                       (unsigned long)snapshot.generation,
                       snapshot.passed_count,
                       snapshot.failed_count,
                       snapshot.not_connected_count,
                       snapshot.not_installed_count,
                       (unsigned long)(snapshot.completed_ms -
                                       snapshot.started_ms));
    for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; ++index)
    {
        const app_self_test_item_t *item = &snapshot.items[index];

        (void)shell_printf(shell,
                           "%-16s %-14s error=%ld value=%lu %s\r\n",
                           app_self_test_item_name(item->id),
                           app_self_test_status_name(item->status),
                           (long)item->error_code,
                           (unsigned long)item->value,
                           item->detail);
    }
    return 0;
}

/** @brief Inspect or request one controlled Stop-mode transaction. */
static int app_shell_cmd_power(shell_t *shell,
                               int argc,
                               char **argv,
                               void *arg)
{
    app_power_snapshot_t snapshot;
    char reason[32];

    (void)arg;
    if((argc == 2) && (strcmp(argv[1], "status") == 0))
    {
        app_power_get_snapshot(&snapshot);
        app_power_format_wake_reason(snapshot.wake_reason,
                                     reason,
                                     (uint32_t)sizeof(reason));
        (void)shell_printf(shell,
                           "power state=%s gen=%lu sleep=%lu/%lu source=0x%02lX armed=0x%02lX wake=%s count=%lu reject=%lu restore_err=%lu error=%ld\r\n",
                           app_power_state_name(snapshot.state),
                           (unsigned long)snapshot.generation,
                           (unsigned long)snapshot.measured_sleep_seconds,
                           (unsigned long)snapshot.requested_sleep_seconds,
                           (unsigned long)snapshot.requested_sources,
                           (unsigned long)snapshot.armed_sources,
                           reason,
                           (unsigned long)snapshot.sleep_count,
                           (unsigned long)snapshot.rejected_count,
                           (unsigned long)snapshot.restore_error_count,
                           (long)snapshot.last_error);
        (void)shell_printf(shell,
                           "power auto=%u idle_ms=%lu max_sleep=%lu auto_count=%lu blocked=%lu locks=0x%02lX deadlines=0x%02lX next_ms=%lu\r\n",
                           (unsigned int)snapshot.auto_enabled,
                           (unsigned long)snapshot.auto_idle_ms,
                           (unsigned long)snapshot.auto_max_sleep_seconds,
                           (unsigned long)snapshot.auto_sleep_count,
                           (unsigned long)snapshot.auto_blocked_count,
                           (unsigned long)snapshot.active_lock_mask,
                           (unsigned long)snapshot.deadline_mask,
                           (unsigned long)snapshot.next_deadline_ms);
        return 0;
    }
    if((argc == 3) && (strcmp(argv[1], "auto") == 0) &&
       (strcmp(argv[2], "off") == 0))
    {
        if(app_power_configure_auto(0U, 10000U, 10U,
                                    APP_POWER_WAKE_SOURCE_RTC) == 0)
        {
            (void)shell_write(shell, "power auto Stop disabled\r\n");
            return 0;
        }
        return -1;
    }
    if((argc == 6) && (strcmp(argv[1], "auto") == 0) &&
       (strcmp(argv[2], "on") == 0))
    {
        uint32_t idle_ms = (uint32_t)strtoul(argv[3], NULL, 0);
        uint32_t max_seconds = (uint32_t)strtoul(argv[4], NULL, 0);
        uint32_t sources;

        if(strcmp(argv[5], "rtc") == 0)
            sources = APP_POWER_WAKE_SOURCE_RTC;
        else if(strcmp(argv[5], "all") == 0)
            sources = APP_POWER_WAKE_SOURCE_RTC |
                      APP_POWER_WAKE_SOURCE_TOUCH |
                      APP_POWER_WAKE_SOURCE_W800;
        else
            sources = 0xFFFFFFFFUL;

        if(app_power_configure_auto(1U, idle_ms, max_seconds, sources) == 0)
        {
            (void)shell_write(shell,
                              "power auto Stop enabled; close the USB terminal to release its wake lock\r\n");
            return 0;
        }
        (void)shell_write(shell, "power auto Stop configuration rejected\r\n");
        return -1;
    }
    if((argc == 4) && (strcmp(argv[1], "lock") == 0) &&
       (strcmp(argv[2], "user") == 0))
    {
        if(strcmp(argv[3], "on") == 0)
        {
            app_power_wake_lock_acquire(APP_POWER_OWNER_USER);
            (void)shell_write(shell, "power user wake lock acquired\r\n");
            return 0;
        }
        if(strcmp(argv[3], "off") == 0)
        {
            app_power_wake_lock_release(APP_POWER_OWNER_USER);
            (void)shell_write(shell, "power user wake lock released\r\n");
            return 0;
        }
    }
    if((argc == 4) && (strcmp(argv[1], "stop") == 0))
    {
        uint32_t seconds = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t sources;

        if(strcmp(argv[3], "rtc") == 0)
            sources = APP_POWER_WAKE_SOURCE_RTC;
        else if(strcmp(argv[3], "touch") == 0)
            sources = APP_POWER_WAKE_SOURCE_TOUCH;
        else if(strcmp(argv[3], "w800") == 0)
            sources = APP_POWER_WAKE_SOURCE_W800;
        else if(strcmp(argv[3], "all") == 0)
            sources = APP_POWER_WAKE_SOURCE_RTC |
                      APP_POWER_WAKE_SOURCE_TOUCH |
                      APP_POWER_WAKE_SOURCE_W800;
        else
            sources = 0xFFFFFFFFUL;

        if(app_power_request_stop(seconds, sources) == 0)
        {
            (void)shell_write(shell,
                              "power Stop request accepted; USB will reconnect after wake\r\n");
            return 0;
        }
        (void)shell_write(shell, "power Stop request rejected\r\n");
        return -1;
    }
    (void)shell_write(shell,
                      "usage: power status|stop <seconds> rtc|touch|w800|all|auto on <idle_ms> <max_seconds> rtc|all|auto off|lock user on|off\r\n");
    return -1;
}

/** @brief Return the stable shell name for one logical UI page. */
static const char *app_shell_ui_page_name(app_ui_page_t page)
{
    if(page == APP_UI_PAGE_COMM)
        return "comm";
    if(page == APP_UI_PAGE_CAN_SELF_TEST)
        return "can";
    if(page == APP_UI_PAGE_BOARD_SELF_TEST)
        return "self_test";
    return "dashboard";
}

/** @brief Inspect assets or request one existing LVGL page. */
static int app_shell_cmd_ui(shell_t *shell, int argc, char **argv, void *arg)
{
    app_ui_page_t page;

    (void)arg;
    if(argc == 2 && strcmp(argv[1], "status") == 0)
    {
        page = app_ui_get_page();
        (void)shell_printf(shell,
                           "ui page=%s asset=%s version=%lu update=%lu/%lu err=%s@0x%08lX\r\n",
                           app_shell_ui_page_name(page),
                           ui_asset_store_status(),
                           (unsigned long)ui_asset_store_active_version(),
                           (unsigned long)ui_asset_update_received(),
                           (unsigned long)ui_asset_update_expected(),
                           ui_asset_update_error(),
                           (unsigned long)ui_asset_update_error_address());
        return 0;
    }

    if(argc == 2 && strcmp(argv[1], "asset") == 0)
    {
        (void)shell_printf(shell,
                           "ui asset available=%u status=%s version=%lu update=%lu/%lu err=%s@0x%08lX\r\n",
                           ui_asset_store_available() ? 1U : 0U,
                           ui_asset_store_status(),
                           (unsigned long)ui_asset_store_active_version(),
                           (unsigned long)ui_asset_update_received(),
                           (unsigned long)ui_asset_update_expected(),
                           ui_asset_update_error(),
                           (unsigned long)ui_asset_update_error_address());
        return 0;
    }

    if(argc == 2 && strcmp(argv[1], "flash") == 0)
    {
        bsp_flash_id_t id = {0};
        static const uint8_t pattern[16] =
        {
            0x55U, 0xAAU, 0x11U, 0x22U, 0x33U, 0x44U, 0x5AU, 0xA5U,
            0xC3U, 0x3CU, 0x78U, 0x87U, 0x00U, 0xFFU, 0x12U, 0x34U
        };
        uint8_t id_ok;
        uint8_t erase_ok;
        uint8_t write_ok;
        uint8_t verify_ok;

        id_ok = bsp_flash_read_id(&id) ? 1U : 0U;
        erase_ok = bsp_flash_erase_4k(UI_ASSET_RESERVED_ADDR) ? 1U : 0U;
        write_ok = erase_ok ? (bsp_flash_write(UI_ASSET_RESERVED_ADDR, pattern, sizeof(pattern)) ? 1U : 0U) : 0U;
        verify_ok = write_ok ? (bsp_flash_read_verify(UI_ASSET_RESERVED_ADDR, pattern, sizeof(pattern)) ? 1U : 0U) : 0U;

        (void)shell_printf(shell,
                           "ui flash jedec=%u %02X %02X %02X test_addr=0x%08lX erase=%u write=%u verify=%u\r\n",
                           id_ok,
                           id.manufacturer_id,
                           id.memory_type,
                           id.capacity,
                           (unsigned long)UI_ASSET_RESERVED_ADDR,
                           erase_ok,
                           write_ok,
                           verify_ok);
        return 0;
    }

    if(argc == 2 && strcmp(argv[1], "next") == 0)
    {
        page = app_ui_get_page();
        app_ui_page_t next_page = APP_UI_PAGE_DASHBOARD;

        if(page == APP_UI_PAGE_DASHBOARD)
            next_page = APP_UI_PAGE_COMM;
        else if(page == APP_UI_PAGE_COMM)
            next_page = APP_UI_PAGE_CAN_SELF_TEST;
        else if(page == APP_UI_PAGE_CAN_SELF_TEST)
            next_page = APP_UI_PAGE_BOARD_SELF_TEST;
        if(app_ui_request_page(next_page) == 0)
        {
            (void)shell_write(shell, "ui next page requested\r\n");
            return 0;
        }
    }

    if(argc == 3 && strcmp(argv[1], "page") == 0)
    {
        if(strcmp(argv[2], "dashboard") == 0)
        {
            if(app_ui_request_page(APP_UI_PAGE_DASHBOARD) == 0)
            {
                (void)shell_write(shell, "ui page dashboard requested\r\n");
                return 0;
            }
        }
        else if(strcmp(argv[2], "comm") == 0)
        {
            if(app_ui_request_page(APP_UI_PAGE_COMM) == 0)
            {
                (void)shell_write(shell, "ui page comm requested\r\n");
                return 0;
            }
        }
        else if(strcmp(argv[2], "can") == 0)
        {
            if(app_ui_request_page(APP_UI_PAGE_CAN_SELF_TEST) == 0)
            {
                (void)shell_write(shell, "ui page can requested\r\n");
                return 0;
            }
        }
        else if(strcmp(argv[2], "self_test") == 0)
        {
            if(app_ui_request_page(APP_UI_PAGE_BOARD_SELF_TEST) == 0)
            {
                (void)shell_write(shell, "ui page self_test requested\r\n");
                return 0;
            }
        }
    }

    (void)shell_write(shell, "usage: ui status|asset|flash|next|page dashboard|page comm|page can|page self_test\r\n");
    return -1;
}

/** @brief Print one coherent dual-FDCAN self-test diagnostics snapshot. */
static int app_shell_cmd_can(shell_t *shell, int argc, char **argv, void *arg)
{
    app_can_self_test_snapshot_t status;

    (void)arg;
    if((argc == 4) && (strcmp(argv[1], "inject") == 0) &&
       (strcmp(argv[2], "bus_off") == 0))
    {
        uint32_t target = (uint32_t)strtoul(argv[3], NULL, 0);
        bsp_status_t result = app_can_self_test_request_bus_off((uint8_t)target);

        if(result == BSP_STATUS_OK)
        {
            (void)shell_printf(shell,
                               "can%lu controlled bus_off requested\r\n",
                               (unsigned long)target);
            return 0;
        }
        (void)shell_printf(shell,
                           "can bus_off request rejected status=%u\r\n",
                           (unsigned int)result);
        return -1;
    }
    if((argc != 2) || (strcmp(argv[1], "status") != 0))
    {
        (void)shell_write(shell,
                          "usage: can status|inject bus_off 1|2\r\n");
        return -1;
    }

    app_can_self_test_get_snapshot(&status);
    (void)shell_printf(shell,
                       "can state=%s status=%u seq=%lu pass=%lu fail=%lu timeout=%lu latency=%lu/%lu us bitrate=%lu/%lu\r\n",
                       app_can_self_test_state_name(status.state),
                       (unsigned)status.last_status,
                       (unsigned long)status.sequence,
                       (unsigned long)status.passed_cycles,
                       (unsigned long)status.failed_cycles,
                       (unsigned long)status.timeout_count,
                       (unsigned long)status.last_latency_us,
                       (unsigned long)status.maximum_latency_us,
                       (unsigned long)status.can1_bitrate_hz,
                       (unsigned long)status.can2_bitrate_hz);
    (void)shell_printf(shell,
                       "can can1 tx=%lu rx=%lu err=%lu bo=%lu tec=%u rec=%u psr_bo=%u\r\n",
                       (unsigned long)status.can1_tx_frames,
                       (unsigned long)status.can1_rx_frames,
                       (unsigned long)status.can1_error_events,
                       (unsigned long)status.can1_bus_off_events,
                       (unsigned int)status.can1_tx_error_count,
                       (unsigned int)status.can1_rx_error_count,
                       (unsigned int)status.can1_protocol_bus_off);
    (void)shell_printf(shell,
                       "can can2 tx=%lu rx=%lu err=%lu bo=%lu tec=%u rec=%u psr_bo=%u\r\n",
                       (unsigned long)status.can2_tx_frames,
                       (unsigned long)status.can2_rx_frames,
                       (unsigned long)status.can2_error_events,
                       (unsigned long)status.can2_bus_off_events,
                       (unsigned int)status.can2_tx_error_count,
                       (unsigned int)status.can2_rx_error_count,
                       (unsigned int)status.can2_protocol_bus_off);
    (void)shell_printf(shell,
                       "can ignored=%lu recovery_fail=%lu\r\n",
                       (unsigned long)status.ignored_frames,
                       (unsigned long)status.recovery_failures);
    (void)shell_printf(shell,
                       "can fault state=%s target=%u request=%lu pass=%lu fail=%lu last_pass=%u status=%u\r\n",
                       app_can_fault_state_name(status.fault_state),
                       (unsigned int)status.fault_target,
                       (unsigned long)status.fault_requests,
                       (unsigned long)status.fault_passes,
                       (unsigned long)status.fault_failures,
                       status.fault_last_passed ? 1U : 0U,
                       (unsigned int)status.fault_last_status);
    return 0;
}

/** @brief Start, inspect, or export one production-test session. */
static int app_shell_cmd_production(shell_t *shell,
                                    int argc,
                                    char **argv,
                                    void *arg)
{
    app_production_report_t report;
    char digest[(APP_PRODUCTION_DIGEST_SIZE * 2U) + 1U] = {0};
    uint32_t index;

    (void)arg;
    if((argc == 2) && (strcmp(argv[1], "run") == 0))
    {
        if(app_production_test_start(bsp_timer_get_ms()))
        {
            (void)shell_write(shell,
                              "production test started; LCD self_test page selected\r\n");
            return 0;
        }
        (void)shell_write(shell, "production test busy\r\n");
        return -1;
    }
    if((argc == 1) ||
       ((argc == 2) && (strcmp(argv[1], "status") == 0)))
    {
        app_production_test_get_report(&report);
        app_production_test_format_digest(&report,
                                          digest,
                                          (uint32_t)sizeof(digest));
        (void)shell_printf(shell,
                           "production state=%s session=%lu device_id=%08lX%08lX%08lX source=%u generation=%lu pass=%u fail=%u offline=%u digest=%s\r\n",
                           app_production_test_state_name(report.state),
                           (unsigned long)report.session_id,
                           (unsigned long)report.device_id[0],
                           (unsigned long)report.device_id[1],
                           (unsigned long)report.device_id[2],
                           (unsigned int)report.device_id_source,
                           (unsigned long)report.self_test.generation,
                           report.self_test.passed_count,
                           report.self_test.failed_count,
                           report.self_test.not_connected_count +
                           report.self_test.not_installed_count,
                           report.digest_valid ? digest : "pending");
        return 0;
    }
    if((argc == 2) && (strcmp(argv[1], "report") == 0))
    {
        app_production_test_get_report(&report);
        if(report.digest_valid == 0U)
        {
            (void)shell_write(shell,
                              "production report not ready\r\n");
            return -1;
        }
        app_production_test_format_digest(&report,
                                          digest,
                                          (uint32_t)sizeof(digest));
        (void)shell_write(shell, "production_report_begin\r\n");
        (void)snprintf(g_app_shell_production_json,
                       sizeof(g_app_shell_production_json),
                       "{\"kind\":\"production_summary\",\"schema\":%lu,\"firmware\":\"%s\",\"session\":%lu,\"state\":\"%s\",\"device_id\":\"%08lX%08lX%08lX\",\"device_id_source\":%u,\"started_ms\":%lu,\"completed_ms\":%lu,\"generation\":%lu,\"passed\":%u,\"failed\":%u,\"not_connected\":%u,\"not_installed\":%u,\"sha256\":\"%s\"}\r\n",
                       (unsigned long)report.schema_version,
                       APP_FIRMWARE_VERSION,
                       (unsigned long)report.session_id,
                       app_production_test_state_name(report.state),
                       (unsigned long)report.device_id[0],
                       (unsigned long)report.device_id[1],
                       (unsigned long)report.device_id[2],
                       (unsigned int)report.device_id_source,
                       (unsigned long)report.started_ms,
                       (unsigned long)report.completed_ms,
                       (unsigned long)report.self_test.generation,
                       report.self_test.passed_count,
                       report.self_test.failed_count,
                       report.self_test.not_connected_count,
                       report.self_test.not_installed_count,
                       digest);
        (void)shell_write(shell, g_app_shell_production_json);
        for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; ++index)
        {
            const app_self_test_item_t *item =
                &report.self_test.items[index];

            (void)snprintf(g_app_shell_production_json,
                           sizeof(g_app_shell_production_json),
                           "{\"kind\":\"production_item\",\"id\":%u,\"name\":\"%s\",\"status\":\"%s\",\"error\":%ld,\"value\":%lu,\"detail\":\"%s\"}\r\n",
                           (unsigned int)item->id,
                           app_self_test_item_name(item->id),
                           app_self_test_status_name(item->status),
                           (long)item->error_code,
                           (unsigned long)item->value,
                           item->detail);
            (void)shell_write(shell, g_app_shell_production_json);
        }
        (void)shell_write(shell, "production_report_end\r\n");
        return 0;
    }
    (void)shell_write(shell,
                      "usage: production run|status|report\r\n");
    return -1;
}

static int app_shell_cmd_touch(shell_t *shell, int argc, char **argv, void *arg)
{
    bsp_touch_state_t state;
    int ret;

    (void)arg;
    if(argc != 2 || (strcmp(argv[1], "status") != 0 && strcmp(argv[1], "read") != 0))
    {
        (void)shell_write(shell, "usage: touch status|read\r\n");
        return -1;
    }

    ret = bsp_touch_read(&state);
    (void)shell_printf(shell,
                       "touch %s present=%u int=%u touched=%u points=%u x=%u y=%u event=%u gesture=0x%02X chip=0x%02X vendor=0x%02X\r\n",
                       ret == 0 ? "ok" : "error",
                       state.present,
                       state.int_active,
                       state.touched,
                       state.points,
                       state.x,
                       state.y,
                       state.event,
                       state.gesture,
                       state.chip_id,
                       state.vendor_id);
    return ret;
}

static const shell_command_t g_app_shell_commands[] =
{
    {"help", "help [command]", "show command help", app_shell_cmd_help, NULL},
    {"version", "version", "show firmware versions", app_shell_cmd_version, NULL},
    {"uptime", "uptime", "show system uptime", app_shell_cmd_uptime, NULL},
    {"clear", "clear", "clear the terminal", app_shell_cmd_clear, NULL},
    {"reboot", "reboot now", "software reset the MCU", app_shell_cmd_reboot, NULL},
    {"log", "log status", "inspect and configure layered logs", app_shell_cmd_log, NULL},
    {"blackbox", "blackbox status|tail|export|mark|clear", "inspect and export persistent RTC events", app_shell_cmd_blackbox, NULL},
    {"rtc", "rtc status|set YYYY-MM-DD HH:MM:SS", "inspect or set retained calendar", app_shell_cmd_rtc, NULL},
    {"self_test", "self_test status|run", "run and print whole-board diagnostics", app_shell_cmd_self_test, NULL},
    {"power", "power status|stop|auto|lock", "control automatic Stop and wake locks", app_shell_cmd_power, NULL},
    {"modbus", "modbus status", "show Modbus RTU counters", app_shell_cmd_modbus, NULL},
    {"wifi", "wifi status|ble|rescue", "inspect WiFi, start BLE provisioning, or USB rescue", app_shell_cmd_wifi, NULL},
    {"mqtt", "mqtt status|reconnect", "inspect or reconnect MQTT", app_shell_cmd_mqtt, NULL},
    {"ota", "ota status", "show OTA receive state", app_shell_cmd_ota, NULL},
    {"usb", "usb status", "show Vendor Bulk counters", app_shell_cmd_usb, NULL},
    {"ui", "ui status|asset|next|page dashboard|page comm|page can|page self_test", "switch LVGL pages", app_shell_cmd_ui, NULL},
    {"can", "can status|inject bus_off 1|2", "diagnose dual FDCAN and controlled faults", app_shell_cmd_can, NULL},
    {"production", "production run|status|report", "run and export whole-board production test", app_shell_cmd_production, NULL},
    {"touch", "touch status|read", "read FT6336U touch state", app_shell_cmd_touch, NULL}
};

static void app_shell_service_once(void)
{
    int length;

    if(g_app_shell_banner_pending != 0U && g_active_transport != APP_SHELL_TRANSPORT_NONE)
    {
        g_app_shell_banner_pending = 0U;
        shell_start(&g_app_shell, g_app_shell_banner);
    }

    for(;;)
    {
        length = ldc_easy_pop(&g_app_shell_ldc, g_app_shell_frame, sizeof(g_app_shell_frame));
        if(length <= 0)
            break;

        app_shell_input(g_active_transport, g_app_shell_frame, (uint16_t)length);
        app_shell_secure_zero(g_app_shell_frame, sizeof(g_app_shell_frame));
    }
}

static void app_shell_thread_entry(ULONG thread_input)
{
    (void)thread_input;

    for(;;)
    {
        if(tx_semaphore_get(&g_app_shell_sem, TX_WAIT_FOREVER) != TX_SUCCESS)
            continue;

        app_shell_service_once();
        while(tx_semaphore_get(&g_app_shell_sem, TX_NO_WAIT) == TX_SUCCESS)
        {
        }
        app_shell_service_once();
    }
}

int app_shell_init(void)
{
    ldc_easy_config_t config;
    UINT status;
    int ret;

    if(g_app_shell_initialized != 0U)
        return 0;

    status = tx_semaphore_create(&g_app_shell_sem, "shell rx", 0U);
    if(status != TX_SUCCESS)
        return -1;

    memset(&config, 0, sizeof(config));
    config.ring_buffer = g_app_shell_ring;
    config.ring_size = sizeof(g_app_shell_ring);
    config.packet_pool = g_app_shell_packets;
    config.packet_count = APP_SHELL_PACKET_COUNT;
    config.max_frame = APP_CONSOLE_LDC_MAX_FRAME;
    config.timeout_ms = APP_CONSOLE_TIMEOUT_MS;
    config.delimiter_enabled = true;
    config.delimiter = (uint8_t)'\n';
    config.mode = LDC_MODE_OVERWRITE;
    config.lock = ldc_port_irq_lock;
    config.unlock = ldc_port_irq_unlock;
    config.event_cb = app_shell_ldc_event;
    config.auto_tick = true;
    if(!ldc_easy_init(&g_app_shell_ldc, &config))
        return -1;

    app_log_init();
    ret = shell_init(&g_app_shell,
                     "leduo> ",
                     app_shell_write,
                     NULL,
                     g_app_shell_commands,
                     (uint16_t)(sizeof(g_app_shell_commands) / sizeof(g_app_shell_commands[0])));
    if(ret == 0)
    {
        app_log_set_sink(app_shell_log_sink, NULL);
        status = tx_thread_create(&g_app_shell_thread,
                                  "App Shell",
                                  app_shell_thread_entry,
                                  0U,
                                  g_app_shell_thread_stack,
                                  sizeof(g_app_shell_thread_stack),
                                  13U,
                                  13U,
                                  TX_NO_TIME_SLICE,
                                  TX_AUTO_START);
        if(status != TX_SUCCESS)
            return -1;
        g_app_shell_initialized = 1U;
    }
    return ret;
}

int app_shell_bind_transport(app_shell_transport_t transport, app_shell_write_fn write, void *arg)
{
    if(transport <= APP_SHELL_TRANSPORT_NONE || transport >= APP_SHELL_TRANSPORT_COUNT || write == NULL)
        return -1;

    g_transport_write[transport] = write;
    g_transport_arg[transport] = arg;
    return 0;
}

static void app_shell_activate(app_shell_transport_t transport)
{
    if(transport <= APP_SHELL_TRANSPORT_NONE ||
       transport >= APP_SHELL_TRANSPORT_COUNT ||
       g_transport_write[transport] == NULL ||
       g_transport_connected[transport] == 0U)
    {
        return;
    }

    g_active_transport = transport;
    shell_reset(&g_app_shell);
    g_app_shell_banner_pending = 1U;
    APP_LOGI(APP_LOG_MODULE_SHELL, "active transport %u", (unsigned int)transport);
    app_shell_wake();
}

void app_shell_connected(app_shell_transport_t transport)
{
    if(transport <= APP_SHELL_TRANSPORT_NONE || transport >= APP_SHELL_TRANSPORT_COUNT)
        return;

    g_transport_connected[transport] = 1U;
    if(g_active_transport == APP_SHELL_TRANSPORT_NONE || transport == APP_SHELL_DEFAULT_TRANSPORT)
        app_shell_activate(transport);
}

void app_shell_disconnected(app_shell_transport_t transport)
{
    if(transport <= APP_SHELL_TRANSPORT_NONE || transport >= APP_SHELL_TRANSPORT_COUNT)
        return;

    g_transport_connected[transport] = 0U;
    if(g_active_transport == transport)
    {
        g_active_transport = APP_SHELL_TRANSPORT_NONE;
        g_app_shell_banner_pending = 0U;
        shell_reset(&g_app_shell);
        (void)ldc_easy_abort(&g_app_shell_ldc);
        if(g_transport_connected[APP_SHELL_DEFAULT_TRANSPORT] != 0U)
            app_shell_activate(APP_SHELL_DEFAULT_TRANSPORT);
    }
}

void app_shell_poll(void)
{
    if(g_app_shell_banner_pending != 0U || ldc_easy_available(&g_app_shell_ldc) != 0U)
        app_shell_wake();
}

bool app_shell_feed(app_shell_transport_t transport, const uint8_t *data, uint16_t length)
{
    uint32_t written;

    if(transport != g_active_transport || data == NULL || length == 0U)
        return false;

    written = ldc_easy_add(&g_app_shell_ldc, data, length);
    if(written != (uint32_t)length)
    {
        (void)ldc_easy_abort(&g_app_shell_ldc);
        APP_LOGW(APP_LOG_MODULE_SHELL, "rx overflow written=%lu len=%u",
                 (unsigned long)written,
                 (unsigned int)length);
        return false;
    }

    return true;
}

void app_shell_input(app_shell_transport_t transport, const uint8_t *data, uint16_t length)
{
    uint8_t banner_was_pending = g_app_shell_banner_pending;

    if(transport != g_active_transport)
        return;

    if(banner_was_pending != 0U && length == 1U && data != NULL && data[0] == '\r')
        return;
    shell_input(&g_app_shell, data, length);
}

bool app_shell_accepts_input(app_shell_transport_t transport, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if(transport != g_active_transport)
        return false;

    if(data == NULL || length == 0U)
        return false;
    if(shell_has_partial_line(&g_app_shell))
        return true;

    for(i = 0U; i < length; i++)
    {
        uint8_t ch = data[i];
        if((ch >= 0x20U && ch <= 0x7EU) || ch == '\r' || ch == '\n' ||
           ch == '\t' || ch == 0x08U || ch == 0x7FU || ch == 0x1BU)
            continue;
        return false;
    }
    return true;
}
