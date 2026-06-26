#include "app_shell.h"

#include <stddef.h>
#include <string.h>

#include "app_board_io.h"
#include "app_config.h"
#include "app_msg_bus_service.h"
#include "app_nearlink.h"
#include "bsp.h"
#include "main.h"
#include "shell.h"
#include "tx_api.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

static shell_t g_app_shell;
static uint8_t g_app_shell_banner_pending;

static const char g_app_shell_banner[] =
    "\r\n\033[1;36m"
    "====================================================\r\n"
    "        ____  _                 ____  _   _         \r\n"
    "       / ___|| |_ _ __ ___     | __ )| | | |        \r\n"
    "       \\___ \\| __| '_ ` _ \\    |  _ \\| | | |        \r\n"
    "        ___) | |_| | | | | |   | |_) | |_| |        \r\n"
    "       |____/ \\__|_| |_| |_|   |____/ \\___/         \r\n"
    "                                                    \r\n"
    "     STM32H563 Industrial Embedded System          \r\n"
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
    return (app_usb_cdc_write(data, length) == UX_SUCCESS) ? (int)length : -1;
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
    (void)argc;
    (void)argv;
    (void)arg;
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

    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: modbus status\r\n");
        return -1;
    }

    app_board_get_status(&status);
    (void)shell_printf(shell,
                       "modbus unit=%u rx=%lu tx=%lu ignored=%lu crc=%lu exceptions=%lu io_errors=%lu\r\n",
                       APP_RS485_MODBUS_UNIT_ID,
                       (unsigned long)status.modbus.rx_frames,
                       (unsigned long)status.modbus.tx_frames,
                       (unsigned long)status.modbus.ignored_frames,
                       (unsigned long)status.modbus.crc_errors,
                       (unsigned long)status.modbus.exceptions,
                       (unsigned long)status.modbus.transport_errors);
    app_shell_print_ldc(shell, "rs485", &status.rs485_ldc);
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
                       APP_W800_WIFI_SSID);
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
    (void)shell_printf(shell, "mqtt %s, broker=%s:%u, socket=%d\r\n",
                       status.mqtt_online ? "online" : "offline",
                       APP_W800_MQTT_HOST,
                       (unsigned int)APP_W800_MQTT_PORT,
                       status.w800_socket_id);
    return 0;
}

static int app_shell_cmd_ota(shell_t *shell, int argc, char **argv, void *arg)
{
    app_board_status_t status;

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

static int app_shell_cmd_bus(shell_t *shell, int argc, char **argv, void *arg)
{
    app_msg_bus_stats_t stats;

    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
    {
        (void)shell_write(shell, "usage: bus status\r\n");
        return -1;
    }

    app_msg_bus_service_get_stats(&stats);
    (void)shell_printf(shell,
                       "bus %s published=%lu dispatched=%lu dropped=%lu handlers=%lu high=%u/%u normal=%u/%u\r\n",
                       APP_ENABLE_MSG_BUS ? "enabled" : "disabled",
                       (unsigned long)stats.published,
                       (unsigned long)stats.dispatched,
                       (unsigned long)stats.dropped,
                       (unsigned long)stats.handler_calls,
                       (unsigned int)stats.high_used,
                       (unsigned int)stats.high_peak,
                       (unsigned int)stats.normal_used,
                       (unsigned int)stats.normal_peak);
    return 0;
}

static int app_shell_cmd_nearlink(shell_t *shell, int argc, char **argv, void *arg)
{
    app_nearlink_status_t status;

    (void)arg;
    if(argc == 2 && strcmp(argv[1], "status") == 0)
    {
        app_nearlink_get_status(&status);
        (void)shell_printf(shell,
                           "nearlink role=%s active=%u connected=%u pending=%u reset=%u local=%s peer=%s last=%s\r\n",
                           status.role == AT_NEARLINK_ROLE_SERVER ? "server" : "client",
                           status.active, status.connected, status.apply_pending, status.reset_pin,
                           status.local_name, status.peer_name,
                           status.last_error ? status.last_error : "none");
        (void)shell_printf(shell,
                           "nearlink diag uart_rx=%lu events=%lu ldc_rx=%lu packets=%lu\r\n",
                           (unsigned long)status.uart_rx_bytes,
                           (unsigned long)status.uart_rx_events,
                           (unsigned long)status.ldc_rx_bytes,
                           (unsigned long)status.ldc_packets);
        return 0;
    }
    if(argc >= 3 && strcmp(argv[1], "role") == 0)
    {
        if(strcmp(argv[2], "server") == 0)
        {
            if(app_nearlink_request_role(AT_NEARLINK_ROLE_SERVER,
                                         argc >= 4 ? argv[3] : NULL, NULL) == 0)
            {
                (void)shell_write(shell, "nearlink server configuration queued\r\n");
                return 0;
            }
        }
        else if(strcmp(argv[2], "client") == 0 && argc >= 4)
        {
            if(app_nearlink_request_role(AT_NEARLINK_ROLE_CLIENT,
                                         argc >= 5 ? argv[4] : NULL, argv[3]) == 0)
            {
                (void)shell_write(shell, "nearlink client configuration queued\r\n");
                return 0;
            }
        }
    }
    if(argc == 3 && strcmp(argv[1], "send") == 0)
    {
        if(app_nearlink_request_send((const uint8_t *)argv[2], (uint16_t)strlen(argv[2])) == 0)
        {
            (void)shell_write(shell, "nearlink send queued\r\n");
            return 0;
        }
        (void)shell_write(shell, "nearlink send queue busy\r\n");
        return -1;
    }

    (void)shell_write(shell,
        "usage: nearlink status|role server [local]|role client <server> [local]|send <text>\r\n");
    return -1;
}

static const shell_command_t g_app_shell_commands[] =
{
    {"help", "help [command]", "show command help", app_shell_cmd_help, NULL},
    {"version", "version", "show firmware versions", app_shell_cmd_version, NULL},
    {"uptime", "uptime", "show system uptime", app_shell_cmd_uptime, NULL},
    {"clear", "clear", "clear the terminal", app_shell_cmd_clear, NULL},
    {"reboot", "reboot", "software reset the MCU", app_shell_cmd_reboot, NULL},
    {"modbus", "modbus status", "show Modbus RTU counters", app_shell_cmd_modbus, NULL},
    {"wifi", "wifi status", "show WiFi state", app_shell_cmd_wifi, NULL},
    {"mqtt", "mqtt status|reconnect", "inspect or reconnect MQTT", app_shell_cmd_mqtt, NULL},
    {"ota", "ota status", "show OTA receive state", app_shell_cmd_ota, NULL},
    {"usb", "usb status", "show Vendor Bulk counters", app_shell_cmd_usb, NULL},
    {"bus", "bus status", "show optional Message Bus counters", app_shell_cmd_bus, NULL},
    {"nearlink", "nearlink status|role server [local]|role client <server> [local]|send <text>",
     "configure and use NearLink SLE", app_shell_cmd_nearlink, NULL}
};

int app_shell_init(void)
{
    return shell_init(&g_app_shell,
                      "leduo-app> ",
                      app_shell_write,
                      NULL,
                      g_app_shell_commands,
                      (uint16_t)(sizeof(g_app_shell_commands) / sizeof(g_app_shell_commands[0])));
}

void app_shell_connected(void)
{
    shell_reset(&g_app_shell);
    g_app_shell_banner_pending = 1U;
}

void app_shell_disconnected(void)
{
    g_app_shell_banner_pending = 0U;
    shell_reset(&g_app_shell);
}

void app_shell_poll(void)
{
    if(g_app_shell_banner_pending != 0U)
    {
        g_app_shell_banner_pending = 0U;
        shell_start(&g_app_shell, g_app_shell_banner);
    }
}

void app_shell_input(const uint8_t *data, uint16_t length)
{
    uint8_t banner_was_pending = g_app_shell_banner_pending;

    app_shell_poll();
    if(banner_was_pending != 0U && length == 1U && data != NULL && data[0] == '\r')
        return;
    shell_input(&g_app_shell, data, length);
}

bool app_shell_accepts_input(const uint8_t *data, uint16_t length)
{
    uint16_t i;

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
