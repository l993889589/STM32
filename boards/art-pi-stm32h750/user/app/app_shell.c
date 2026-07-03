#include "app_shell.h"

#include <stddef.h>
#include <string.h>

#include "app_config.h"
#include "bsp.h"
#include "shell.h"
#include "tx_api.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

static shell_t g_app_shell;
static TX_QUEUE g_app_shell_rx_queue;
static TX_THREAD g_app_shell_thread;
static ULONG g_app_shell_queue_storage[APP_SHELL_QUEUE_DEPTH];
static UCHAR g_app_shell_stack[APP_SHELL_STACK_SIZE];
static uint8_t g_app_shell_rx_buf[APP_SHELL_RX_BUF_SIZE];
static uint8_t g_app_shell_initialized;

static const char g_app_shell_banner[] =
    "\r\n"
    "====================================================\r\n"
    " LeduO ART-Pi shell\r\n"
    "====================================================\r\n"
    " MCU  : STM32H750\r\n"
    " FW   : " APP_FIRMWARE_VERSION "\r\n"
    " UART : " APP_SHELL_UART_NAME " " APP_SHELL_UART_BAUDRATE_TEXT "-8N1\r\n"
    "\r\n"
    "Type 'help' for commands.\r\n\r\n";

static int app_shell_write(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    return bsp_uart_write_wait_complete(APP_SHELL_UART_PORT,
                                        data,
                                        length,
                                        APP_SHELL_TX_TIMEOUT_MS);
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

static const shell_command_t g_app_shell_commands[] =
{
    {"help", "help [command]", "show command help", app_shell_cmd_help, NULL},
    {"version", "version", "show firmware versions", app_shell_cmd_version, NULL},
    {"uptime", "uptime", "show system uptime", app_shell_cmd_uptime, NULL},
    {"clear", "clear", "clear the terminal", app_shell_cmd_clear, NULL},
    {"reboot", "reboot", "software reset the MCU", app_shell_cmd_reboot, NULL}
};

static void app_shell_uart_rx(bsp_uart_port_t port, const uint8_t *data, uint16_t len, void *arg)
{
    (void)port;
    (void)arg;

    if(data == NULL)
        return;

    for(uint16_t i = 0U; i < len; i++)
    {
        ULONG msg = data[i];
        (void)tx_queue_send(&g_app_shell_rx_queue, &msg, TX_NO_WAIT);
    }
}

static void app_shell_thread_entry(ULONG thread_input)
{
    (void)thread_input;

    (void)bsp_uart_start_rx(APP_SHELL_UART_PORT, g_app_shell_rx_buf, sizeof(g_app_shell_rx_buf));
    shell_start(&g_app_shell, g_app_shell_banner);
    for(;;)
    {
        ULONG msg;
        if(tx_queue_receive(&g_app_shell_rx_queue, &msg, TX_WAIT_FOREVER) == TX_SUCCESS)
        {
            uint8_t ch = (uint8_t)msg;
            shell_input(&g_app_shell, &ch, 1U);
        }
    }
}

int app_shell_init(void)
{
    if(g_app_shell_initialized != 0U)
        return 0;

    if(shell_init(&g_app_shell,
                  "artpi> ",
                  app_shell_write,
                  NULL,
                  g_app_shell_commands,
                  (uint16_t)(sizeof(g_app_shell_commands) / sizeof(g_app_shell_commands[0]))) != 0)
        return -1;

    if(tx_queue_create(&g_app_shell_rx_queue,
                       "shell rx",
                       TX_1_ULONG,
                       g_app_shell_queue_storage,
                       sizeof(g_app_shell_queue_storage)) != TX_SUCCESS)
        return -1;

    if(bsp_uart_register_rx_callback(APP_SHELL_UART_PORT, app_shell_uart_rx, NULL) != 0)
        return -1;

    if(tx_thread_create(&g_app_shell_thread,
                        "UART4 shell",
                        app_shell_thread_entry,
                        0U,
                        g_app_shell_stack,
                        sizeof(g_app_shell_stack),
                        APP_SHELL_THREAD_PRIO,
                        APP_SHELL_THREAD_PRIO,
                        TX_NO_TIME_SLICE,
                        TX_AUTO_START) != TX_SUCCESS)
        return -1;

    g_app_shell_initialized = 1U;
    return 0;
}

void app_shell_connected(void)
{
    shell_reset(&g_app_shell);
}

void app_shell_disconnected(void)
{
    shell_reset(&g_app_shell);
}

void app_shell_poll(void)
{
}

void app_shell_input(const uint8_t *data, uint16_t length)
{
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
