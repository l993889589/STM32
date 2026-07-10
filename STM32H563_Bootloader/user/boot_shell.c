#include "boot_shell.h"

#include <stddef.h>
#include <string.h>

#include "boot_usb_recovery.h"
#include "boot_protection.h"
#include "boot_security.h"
#include "gd25lq128.h"
#include "main.h"
#include "ota_boot_control.h"
#include "ota_layout.h"
#include "shell.h"

#define BOOT_SHELL_VERSION             "2.0.0"
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

static uint8_t boot_shell_control_read(
    void *context, uint32_t address, uint8_t *data, uint32_t size)
{
    (void)context;
    return gd25lq128_read(address, data, size) ? 1U : 0U;
}

static uint8_t boot_shell_control_erase(void *context, uint32_t address)
{
    (void)context;
    return gd25lq128_erase_4k(address) ? 1U : 0U;
}

static uint8_t boot_shell_control_write(
    void *context, uint32_t address, const uint8_t *data, uint32_t size)
{
    (void)context;
    return gd25lq128_write(address, data, size) ? 1U : 0U;
}

static int boot_cmd_ota(shell_t *shell, int argc, char **argv, void *arg)
{
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t record;
    ota_control_copy_t copy;
    uint8_t recovery_active;
    uint32_t recovery_slot;
    uint32_t recovery_received;
    uint32_t recovery_expected;

    (void)arg;
    if(argc != 2 || strcmp(argv[1], "status") != 0)
        return shell_write(shell, "usage: ota status\r\n");

    boot_usb_recovery_get_progress(
        &recovery_active, &recovery_slot, &recovery_received, &recovery_expected);
    (void)shell_printf(shell,
                       "last result=%u reset=0x%08lX recovery=%u slot=%lu received=%lu/%lu\r\n",
                       (unsigned int)g_boot_result,
                       (unsigned long)RCC->RSR,
                       recovery_active,
                       (unsigned long)recovery_slot,
                       (unsigned long)recovery_received,
                       (unsigned long)recovery_expected);

    storage.context = NULL;
    storage.read = boot_shell_control_read;
    storage.erase_sector = boot_shell_control_erase;
    storage.write = boot_shell_control_write;
    if(ota_boot_control_storage_load(&storage, &record, &copy) !=
       OTA_CONTROL_STATUS_OK)
    {
        return shell_write(shell, "control unavailable\r\n");
    }

    (void)shell_printf(shell,
                       "control copy=%u seq=%lu state=%lu active=%lu pending=%lu trial=%lu/%lu min=%lu boot=%lu error=%lu@0x%08lX\r\n",
                       (unsigned int)copy,
                       (unsigned long)record.sequence,
                       (unsigned long)record.state,
                       (unsigned long)record.active_slot,
                       (unsigned long)record.pending_slot,
                       (unsigned long)record.trial_boot_count,
                       (unsigned long)record.trial_boot_limit,
                       (unsigned long)record.minimum_version,
                       (unsigned long)record.boot_count,
                       (unsigned long)record.last_error,
                       (unsigned long)record.last_error_address);
    (void)shell_printf(shell,
                       "slotA state=%lu version=%lu size=%lu flags=0x%08lX crc=0x%08lX\r\n"
                       "slotB state=%lu version=%lu size=%lu flags=0x%08lX crc=0x%08lX\r\n",
                       (unsigned long)record.slots[0].state,
                       (unsigned long)record.slots[0].image_version,
                       (unsigned long)record.slots[0].image_size,
                       (unsigned long)record.slots[0].image_flags,
                       (unsigned long)record.slots[0].image_crc32,
                       (unsigned long)record.slots[1].state,
                       (unsigned long)record.slots[1].image_version,
                       (unsigned long)record.slots[1].image_size,
                       (unsigned long)record.slots[1].image_flags,
                       (unsigned long)record.slots[1].image_crc32);
    return 0;
}

static int boot_cmd_security(shell_t *shell, int argc, char **argv, void *arg)
{
    boot_protection_status_t protection;
    boot_security_diagnostics_t diagnostics;
    ota_boot_control_storage_t storage;
    ota_boot_control_record_t record;
    ota_control_copy_t copy;
    uint32_t verify_error = 0U;
    uint32_t verify_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;
    uint8_t verify_result = 0U;

    (void)arg;
    if(argc > 2 || (argc == 2 && strcmp(argv[1], "verify") != 0))
    {
        return shell_write(shell, "usage: security [verify]\r\n");
    }
    if(argc == 2)
    {
        storage.context = NULL;
        storage.read = boot_shell_control_read;
        storage.erase_sector = boot_shell_control_erase;
        storage.write = boot_shell_control_write;
        if(ota_boot_control_storage_load(&storage, &record, &copy) !=
           OTA_CONTROL_STATUS_OK)
        {
            return shell_write(shell, "security verify: control unavailable\r\n");
        }
        if(record.pending_slot == (uint32_t)OTA_FIRMWARE_SLOT_A ||
           record.pending_slot == (uint32_t)OTA_FIRMWARE_SLOT_B)
        {
            verify_slot = record.pending_slot;
        }
        else if(record.slots[0].state != (uint32_t)OTA_SLOT_STATE_EMPTY)
        {
            verify_slot = (uint32_t)OTA_FIRMWARE_SLOT_A;
        }
        else if(record.slots[1].state != (uint32_t)OTA_SLOT_STATE_EMPTY)
        {
            verify_slot = (uint32_t)OTA_FIRMWARE_SLOT_B;
        }
        if(verify_slot == (uint32_t)OTA_FIRMWARE_SLOT_NONE)
        {
            return shell_write(shell, "security verify: no firmware slot\r\n");
        }
        verify_result = boot_security_verify_slot(&record, verify_slot, &verify_error);
    }
    boot_protection_get_status(&protection);
    boot_security_get_diagnostics(&diagnostics);
    (void)shell_printf(shell,
                       "signature=ECDSA-P256 key=9B6DB882D4D4D93F floor=%lu wrp=%s mask=0x%08lX required=0x%08lX\r\n",
                       (unsigned long)OTA_BOOT_COMPILED_MINIMUM_VERSION,
                       protection.boot_fully_protected ? "protected" : "open",
                       (unsigned long)protection.protected_sector_groups,
                       (unsigned long)protection.required_sector_groups);
    (void)shell_printf(shell,
                       "verify backend=%s attempted=%u status=%lu valid=%u digest=%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
                       diagnostics.backend == BOOT_SECURITY_BACKEND_MICRO_ECC ?
                           "micro-ecc" : "none",
                       diagnostics.attempted,
                       (unsigned long)diagnostics.verify_status,
                       diagnostics.signature_valid,
                       diagnostics.digest[0], diagnostics.digest[1],
                       diagnostics.digest[2], diagnostics.digest[3],
                       diagnostics.digest[4], diagnostics.digest[5],
                       diagnostics.digest[6], diagnostics.digest[7]);
    if(argc == 2)
    {
        return shell_printf(shell, "verify slot=%lu result=%u error=%lu\r\n",
                            (unsigned long)verify_slot,
                            verify_result,
                            (unsigned long)verify_error);
    }
    return 0;
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
    {"security", "security [verify]", "show or run firmware signature verification", boot_cmd_security, NULL},
    {"flash-info", "flash-info", "show external flash identity", boot_cmd_flash, NULL},
    {"boot", "boot", "reboot into a valid application", boot_cmd_boot, NULL},
    {"reboot", "reboot", "software reset the MCU", boot_cmd_reboot, NULL},
    {"clear", "clear", "clear the terminal", boot_cmd_clear, NULL}
};

UINT boot_shell_init(void)
{
    (void)boot_usb_recovery_init_default();
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
    if(boot_usb_recovery_feed(data, length, boot_shell_write, NULL) != 0U)
        return;
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
