#include "app_shell.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "app_board_io.h"
#include "app_config.h"
#include "app_firmware_update_service.h"
#include "app_health.h"
#include "app_log.h"
#include "app_rs485.h"
#include "app_ui.h"
#include "app_w800.h"
#include "bsp.h"
#include "ldc_easy.h"
#include "ldc_port_irq.h"
#include "main.h"
#include "gd25lq128.h"
#include "../../../shared/ota/ota_boot_control.h"
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
static TX_THREAD g_app_shell_thread;
static TX_SEMAPHORE g_app_shell_sem;
static UCHAR g_app_shell_thread_stack[APP_SHELL_THREAD_STACK];
static app_shell_write_fn g_transport_write[APP_SHELL_TRANSPORT_COUNT];
static void *g_transport_arg[APP_SHELL_TRANSPORT_COUNT];
static uint8_t g_transport_connected[APP_SHELL_TRANSPORT_COUNT];
static app_shell_transport_t g_active_transport = APP_SHELL_TRANSPORT_NONE;
static uint8_t g_app_shell_banner_pending;
static uint8_t g_app_shell_initialized;

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
    NVIC_SystemReset();
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
    if(app_rs485_format_network_payload(APP_RS485_NET_STATUS, payload, sizeof(payload)) > 0)
        (void)shell_printf(shell, "modbus status-json %s\r\n", payload);
    if(app_rs485_format_network_payload(APP_RS485_NET_DATA, payload, sizeof(payload)) > 0)
        (void)shell_printf(shell, "modbus data-json %s\r\n", payload);
    return 0;
}

static int app_shell_cmd_wifi(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;

    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: wifi status\r\n");
        return -1;
    }

    app_board_get_status(&status);
    (void)shell_printf(shell, "wifi %s, ssid=%s\r\n",
                       status.wifi_ready ? "ready" : "offline",
                       app_w800_wifi_ssid());
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
    return gd25lq128_read(address, data, size) ? 1U : 0U;
}

static uint8_t app_shell_control_erase(void *context, uint32_t address)
{
    (void)context;
    return gd25lq128_erase_4k(address) ? 1U : 0U;
}

static uint8_t app_shell_control_write(
    void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    (void)context;
    return gd25lq128_write(address, data, size) ? 1U : 0U;
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

static int app_shell_cmd_ui(shell_t *shell, int argc, char **argv, void *arg)
{
    app_ui_page_t page;

    (void)arg;
    if(argc == 2 && strcmp(argv[1], "status") == 0)
    {
        page = app_ui_get_page();
        (void)shell_printf(shell,
                           "ui page=%s asset=%s version=%lu update=%lu/%lu err=%s@0x%08lX\r\n",
                           page == APP_UI_PAGE_COMM ? "comm" : "dashboard",
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
        gd25lq128_id_t id = {0};
        static const uint8_t pattern[16] =
        {
            0x55U, 0xAAU, 0x11U, 0x22U, 0x33U, 0x44U, 0x5AU, 0xA5U,
            0xC3U, 0x3CU, 0x78U, 0x87U, 0x00U, 0xFFU, 0x12U, 0x34U
        };
        uint8_t id_ok;
        uint8_t erase_ok;
        uint8_t write_ok;
        uint8_t verify_ok;

        id_ok = gd25lq128_read_id(&id) ? 1U : 0U;
        erase_ok = gd25lq128_erase_4k(UI_ASSET_RESERVED_ADDR) ? 1U : 0U;
        write_ok = erase_ok ? (gd25lq128_write(UI_ASSET_RESERVED_ADDR, pattern, sizeof(pattern)) ? 1U : 0U) : 0U;
        verify_ok = write_ok ? (gd25lq128_read_verify(UI_ASSET_RESERVED_ADDR, pattern, sizeof(pattern)) ? 1U : 0U) : 0U;

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
        if(app_ui_request_page(page == APP_UI_PAGE_COMM ? APP_UI_PAGE_DASHBOARD : APP_UI_PAGE_COMM) == 0)
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
    }

    (void)shell_write(shell, "usage: ui status|asset|flash|next|page dashboard|page comm\r\n");
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
    {"modbus", "modbus status", "show Modbus RTU counters", app_shell_cmd_modbus, NULL},
    {"wifi", "wifi status", "show WiFi state", app_shell_cmd_wifi, NULL},
    {"mqtt", "mqtt status|reconnect", "inspect or reconnect MQTT", app_shell_cmd_mqtt, NULL},
    {"ota", "ota status", "show OTA receive state", app_shell_cmd_ota, NULL},
    {"usb", "usb status", "show Vendor Bulk counters", app_shell_cmd_usb, NULL},
    {"ui", "ui status|asset|next|page dashboard|page comm", "switch LVGL pages", app_shell_cmd_ui, NULL},
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
