#include "boot_shell.h"

#include <stddef.h>
#include <string.h>

#include "gd25lq128.h"
#include "main.h"
#include "ota_layout.h"
#include "shell.h"

#define BOOT_SHELL_VERSION             "1.0.0"
#define BOOT_SHELL_TX_WAIT_TICKS       100U
#define BOOT_SHELL_LED_TOGGLE_TICKS    500U

static TX_MUTEX g_boot_shell_tx_mutex;
static UX_SLAVE_CLASS_CDC_ACM *g_boot_shell_cdc;
static shell_t g_boot_shell;
static ota_boot_result_t g_boot_result;
static uint8_t g_banner_pending;
static uint8_t g_cdc_dtr;

static const char g_boot_banner[] =
    "\r\n\033[33m"
    " _          _       ___  \r\n"
    "| |    ___ | | ___ / _ \\ \r\n"
    "| |   / _ \\| |/ /| | | |\r\n"
    "| |__|  __/  < | |_| |\r\n"
    "|_____\\___|_|\\_\\ \\___/ \r\n"
    "\033[0m"
    "LeduO STM32H563 Bootloader " BOOT_SHELL_VERSION "\r\n"
    "Maintenance mode. Type 'help' for commands.\r\n\r\n";

static int boot_shell_write(const uint8_t *data, uint16_t length, void *arg)
{
    UX_SLAVE_CLASS_CDC_ACM *instance = g_boot_shell_cdc;
    ULONG actual = 0U;
    UINT status;

    (void)arg;
    if(instance == UX_NULL || data == NULL || length == 0U)
        return -1;
    if(tx_mutex_get(&g_boot_shell_tx_mutex, BOOT_SHELL_TX_WAIT_TICKS) != TX_SUCCESS)
        return -1;
    status = ux_device_class_cdc_acm_write(instance, (UCHAR *)data, length, &actual);
    (void)tx_mutex_put(&g_boot_shell_tx_mutex);
    return (status == UX_SUCCESS && actual == length) ? (int)length : -1;
}

static int boot_cmd_help(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)arg;
    shell_show_help(shell, argc > 1 ? argv[1] : NULL);
    return 0;
}

static int boot_cmd_version(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    return shell_printf(shell, "bootloader %s\r\n", BOOT_SHELL_VERSION);
}

static int boot_cmd_slot(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    return shell_printf(shell, "app base=0x%08lX size=%lu KiB valid=%s\r\n",
                        (unsigned long)OTA_APP_BASE,
                        (unsigned long)(OTA_APP_SIZE / 1024U),
                        ota_boot_app_is_valid() ? "yes" : "no");
}

static int boot_cmd_verify(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    return shell_printf(shell, "app vector %s\r\n",
                        ota_boot_app_is_valid() ? "valid" : "invalid");
}

static int boot_cmd_ota(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
        return shell_write(shell, "usage: ota status\r\n");
    return shell_printf(shell, "last boot result=%u\r\n", (unsigned int)g_boot_result);
}

static int boot_cmd_flash(shell_t *shell, int argc, char **argv, void *arg)
{
    gd25lq128_id_t id;

    (void)argc;
    (void)argv;
    (void)arg;
    if(!gd25lq128_read_id(&id))
        return shell_write(shell, "external flash unavailable\r\n");
    return shell_printf(shell, "gd25 jedec=%02X %02X %02X size=%lu KiB\r\n",
                        id.manufacturer_id, id.memory_type, id.capacity,
                        (unsigned long)(OTA_EXT_FLASH_SIZE / 1024U));
}

static int boot_cmd_boot(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    if(!ota_boot_app_is_valid())
        return shell_write(shell, "app is invalid; staying in maintenance mode\r\n");
    (void)shell_write(shell, "rebooting into app...\r\n");
    tx_thread_sleep(20U);
    NVIC_SystemReset();
    return 0;
}

static int boot_cmd_reboot(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    (void)shell_write(shell, "rebooting...\r\n");
    tx_thread_sleep(20U);
    NVIC_SystemReset();
    return 0;
}

static int boot_cmd_clear(shell_t *shell, int argc, char **argv, void *arg)
{
    (void)argc;
    (void)argv;
    (void)arg;
    return shell_write(shell, "\033[2J\033[H");
}

static const shell_command_t g_boot_commands[] =
{
    {"help", "help [command]", "show command help", boot_cmd_help, NULL},
    {"version", "version", "show bootloader version", boot_cmd_version, NULL},
    {"slot", "slot", "show application slot", boot_cmd_slot, NULL},
    {"verify", "verify", "validate the application vector", boot_cmd_verify, NULL},
    {"ota", "ota status", "show the last OTA boot result", boot_cmd_ota, NULL},
    {"flash-info", "flash-info", "show external flash identity", boot_cmd_flash, NULL},
    {"boot", "boot", "reboot into a valid application", boot_cmd_boot, NULL},
    {"reboot", "reboot", "software reset the MCU", boot_cmd_reboot, NULL},
    {"clear", "clear", "clear the terminal", boot_cmd_clear, NULL}
};

UINT boot_shell_init(void)
{
    if(tx_mutex_create(&g_boot_shell_tx_mutex, "boot shell tx", TX_INHERIT) != TX_SUCCESS)
        return TX_MUTEX_ERROR;
    if(shell_init(&g_boot_shell,
                  "leduo-boot> ",
                  boot_shell_write,
                  NULL,
                  g_boot_commands,
                  (uint16_t)(sizeof(g_boot_commands) / sizeof(g_boot_commands[0]))) != 0)
        return TX_PTR_ERROR;
    return TX_SUCCESS;
}

void boot_shell_set_boot_result(ota_boot_result_t result)
{
    g_boot_result = result;
}

void boot_shell_usb_activate(UX_SLAVE_CLASS_CDC_ACM *instance)
{
    g_boot_shell_cdc = instance;
    g_cdc_dtr = 0U;
    shell_reset(&g_boot_shell);
    g_banner_pending = 0U;
}

void boot_shell_usb_deactivate(UX_SLAVE_CLASS_CDC_ACM *instance)
{
    if(g_boot_shell_cdc == instance)
        g_boot_shell_cdc = UX_NULL;
    g_cdc_dtr = 0U;
    g_banner_pending = 0U;
    shell_reset(&g_boot_shell);
}

void boot_shell_usb_parameter_change(UX_SLAVE_CLASS_CDC_ACM *instance)
{
    UX_SLAVE_CLASS_CDC_ACM_LINE_STATE_PARAMETER line_state = {0};

    if(instance == UX_NULL ||
       ux_device_class_cdc_acm_ioctl(instance,
                                     UX_SLAVE_CLASS_CDC_ACM_IOCTL_GET_LINE_STATE,
                                     &line_state) != UX_SUCCESS)
        return;

    if(line_state.ux_slave_class_cdc_acm_parameter_dtr != 0U)
    {
        if(g_cdc_dtr == 0U)
        {
            shell_reset(&g_boot_shell);
            g_banner_pending = 1U;
        }
        g_cdc_dtr = 1U;
    }
    else
    {
        g_cdc_dtr = 0U;
        g_banner_pending = 0U;
        shell_reset(&g_boot_shell);
    }
}

UX_SLAVE_CLASS_CDC_ACM *boot_shell_usb_instance(void)
{
    return g_boot_shell_cdc;
}

void boot_shell_usb_process(const uint8_t *data, uint32_t length)
{
    uint8_t banner_was_pending = g_banner_pending;

    boot_shell_service();
    if(banner_was_pending != 0U && length == 1U && data != NULL && data[0] == '\r')
        return;
    while(length != 0U)
    {
        uint16_t chunk = length > UINT16_MAX ? UINT16_MAX : (uint16_t)length;
        shell_input(&g_boot_shell, data, chunk);
        data += chunk;
        length -= chunk;
    }
}

void boot_shell_service(void)
{
    if(g_banner_pending != 0U && g_boot_shell_cdc != UX_NULL)
    {
        g_banner_pending = 0U;
        shell_start(&g_boot_shell, g_boot_banner);
    }
}

void boot_shell_led_task(ULONG input)
{
    (void)input;
    for(;;)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_12);
        tx_thread_sleep(BOOT_SHELL_LED_TOGGLE_TICKS);
    }
}
