#include "includes.h"
#include "app_device_config.h"
#include "app_flash_check.h"
#include "app_netx.h"
#include "app_modbus_rtu.h"
#include "app_wifi.h"

#define STARTUP_TASK_PRIORITY       2U
#define LED_TASK_PRIORITY           5U

#define STARTUP_TASK_STACK_SIZE  8192U
#define LED_TASK_STACK_SIZE      1024U

static TX_THREAD startup_task_control_block;
static TX_THREAD led_task_control_block;

static uint64_t startup_task_stack[STARTUP_TASK_STACK_SIZE / sizeof(uint64_t)];
static uint64_t led_task_stack[LED_TASK_STACK_SIZE / sizeof(uint64_t)];

static void startup_task_entry(ULONG thread_input);
static void led_task_entry(ULONG thread_input);
static void application_tasks_create(void);
static void spi_flash_report(void);

int main(void)
{
    system_init();

    tx_kernel_enter();

    while (1)
    {
    }
}

void tx_application_define(void *first_unused_memory)
{
    (void)first_unused_memory;

    (void)tx_thread_create(&startup_task_control_block,
                           "startup_task",
                           startup_task_entry,
                           0UL,
                           startup_task_stack,
                           sizeof(startup_task_stack),
                           STARTUP_TASK_PRIORITY,
                           STARTUP_TASK_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
}

static void startup_task_entry(ULONG thread_input)
{
    (void)thread_input;

    bsp_init();
    application_tasks_create();

    bsp_uart_write_string(BSP_UART_DEBUG,
                          "\r\nhello from ART-Pi STM32H750 ThreadX\r\n");
    bsp_uart_write_string(BSP_UART_DEBUG,
                          "UART4: PA0/TX PI9/RX, LED: PI8 PC15\r\n");
    bsp_uart_write_string(BSP_UART_DEBUG,
                          "DWT delay ready, TIM2 1MHz timer ready, PWM: P1 pin 32/PH10\r\n");

    bsp_beep_on();
    (void)tx_thread_sleep(100U);
    bsp_beep_off();
    bsp_uart_write_string(BSP_UART_DEBUG,
                          "INDUSTRY-IO: buzzer PH7 self-test complete\r\n");

    spi_flash_report();
    (void)app_wifi_start_and_scan();

    if (app_netx_start() != NX_SUCCESS)
    {
        bsp_uart_write_string(BSP_UART_DEBUG,
                              "NetX Duo Ethernet start failed\r\n");
    }

    if (app_modbus_rtu_start() == HAL_OK)
    {
        app_modbus_rtu_config_t config;
        app_device_config_diagnostics_t storage;
        char message[128];

        if (app_modbus_rtu_get_config(&config) != HAL_OK)
        {
            app_device_config_set_defaults(&config.persistent);
        }
        (void)snprintf(message,
                       sizeof(message),
                       "Modbus RTU ready: UART5 PB13/TX PB12/RX, PI4 DE, "
                       "115200 8N1, role=%s, slave_unit=%u, devices=%u\r\n",
                       (config.persistent.rs485_role == APP_RS485_ROLE_MASTER) ?
                           "master" : "slave",
                       (unsigned int)config.persistent.modbus_unit_id,
                       (unsigned int)config.persistent.master_device_count);
        bsp_uart_write_string(BSP_UART_DEBUG, message);
        if (app_device_config_get_diagnostics(&storage) == HAL_OK)
        {
            (void)snprintf(message,
                           sizeof(message),
                           "Config slots: A=%u/v%u/role%u/unit%u/dev%u/seq%lu, "
                           "B=%u/v%u/role%u/unit%u/dev%u/seq%lu\r\n",
                           (unsigned int)storage.slot_a_valid,
                           (unsigned int)storage.slot_a_version,
                           (unsigned int)storage.slot_a_rs485_role,
                           (unsigned int)storage.slot_a_modbus_unit_id,
                           (unsigned int)storage.slot_a_master_device_count,
                           (unsigned long)storage.slot_a_sequence,
                           (unsigned int)storage.slot_b_valid,
                           (unsigned int)storage.slot_b_version,
                           (unsigned int)storage.slot_b_rs485_role,
                           (unsigned int)storage.slot_b_modbus_unit_id,
                           (unsigned int)storage.slot_b_master_device_count,
                           (unsigned long)storage.slot_b_sequence);
            bsp_uart_write_string(BSP_UART_DEBUG, message);
        }
        bsp_uart_write_string(BSP_UART_DEBUG,
                              "Blue LED is the 500 ms heartbeat; slave coils 0/1 "
                              "control red LED/buzzer; HTTP configures RS485 mode\r\n");
    }
    else
    {
        bsp_uart_write_string(BSP_UART_DEBUG,
                              "Modbus RTU start failed\r\n");
    }

    while (1)
    {
        tx_thread_sleep(1000U);
    }
}

static void led_task_entry(ULONG thread_input)
{
    (void)thread_input;

    bsp_led_off(BSP_LED_BLUE);

    while (1)
    {
        (void)tx_thread_sleep(500U);
        bsp_led_toggle(BSP_LED_BLUE);
    }
}

static void application_tasks_create(void)
{
    (void)tx_thread_create(&led_task_control_block,
                           "led_heartbeat_task",
                           led_task_entry,
                           0UL,
                           led_task_stack,
                           sizeof(led_task_stack),
                           LED_TASK_PRIORITY,
                           LED_TASK_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
}

static void spi_flash_report(void)
{
    uint32_t jedec_id = 0U;
    uint32_t wifi_crc32 = 0U;
    uint32_t bt_crc32 = 0U;
    uint8_t wifi_header[16];
    uint8_t bt_header[16];
    HAL_StatusTypeDef status;
    char message[192];

    status = bsp_spi_bus_init();
    if (status == HAL_OK)
    {
        status = bsp_w25q128_init();
    }
    if (status == HAL_OK)
    {
        status = bsp_w25q128_read_id(&jedec_id);
    }
    if (status == HAL_OK)
    {
        status = bsp_w25q128_read(0x000000U,
                                  wifi_header,
                                  sizeof(wifi_header));
    }
    if (status == HAL_OK)
    {
        status = bsp_w25q128_read(0x080000U,
                                  bt_header,
                                  sizeof(bt_header));
    }
    if (status == HAL_OK)
    {
        status = app_flash_crc32(BSP_FLASH_WIFI_IMAGE_ADDRESS,
                                 BSP_FLASH_WIFI_IMAGE_SIZE,
                                 &wifi_crc32);
    }
    if (status == HAL_OK)
    {
        status = app_flash_crc32(BSP_FLASH_BT_IMAGE_ADDRESS,
                                 BSP_FLASH_BT_IMAGE_SIZE,
                                 &bt_crc32);
    }

    if (status != HAL_OK)
    {
        (void)snprintf(message,
                       sizeof(message),
                       "SPI1/W25Q128 init failed, HAL status=%u\r\n",
                       (unsigned int)status);
        bsp_uart_write_string(BSP_UART_DEBUG, message);
        return;
    }

    (void)snprintf(message,
                   sizeof(message),
                   "SPI1 mode=%s, W25Q128 JEDEC=%06lX\r\n"
                   "WiFi[0]=%02X %02X %02X %02X, BT[0]=%02X %02X %02X %02X\r\n",
                   (bsp_spi_bus_get_transfer_mode() == BSP_SPI_TRANSFER_MODE_DMA) ?
                       "DMA" :
                       ((bsp_spi_bus_get_transfer_mode() == BSP_SPI_TRANSFER_MODE_INTERRUPT) ?
                            "INT" : "POLL"),
                   (unsigned long)jedec_id,
                   wifi_header[0],
                   wifi_header[1],
                   wifi_header[2],
                   wifi_header[3],
                   bt_header[0],
                   bt_header[1],
                   bt_header[2],
                   bt_header[3]);
    bsp_uart_write_string(BSP_UART_DEBUG, message);

    (void)snprintf(message,
                   sizeof(message),
                   "WiFi CRC32=%08lX expected=%08lX [%s]\r\n"
                   "BT   CRC32=%08lX expected=%08lX [%s]\r\n",
                   (unsigned long)wifi_crc32,
                   (unsigned long)BSP_FLASH_WIFI_IMAGE_EXPECTED_CRC32,
                   (wifi_crc32 == BSP_FLASH_WIFI_IMAGE_EXPECTED_CRC32) ?
                       "OK" : "MISMATCH",
                   (unsigned long)bt_crc32,
                   (unsigned long)BSP_FLASH_BT_IMAGE_EXPECTED_CRC32,
                   (bt_crc32 == BSP_FLASH_BT_IMAGE_EXPECTED_CRC32) ?
                       "OK" : "MISMATCH");
    bsp_uart_write_string(BSP_UART_DEBUG, message);

#if APP_FLASH_DESTRUCTIVE_TEST_ENABLE
    {
        app_flash_test_result_t test_result;

        status = app_flash_run_safe_test(&test_result);
        (void)snprintf(message,
                       sizeof(message),
                       "W25Q128 safe test: status=%u stage=%s "
                       "crc=%08lX/%08lX cleanup=%u erased=%u\r\n",
                       (unsigned int)status,
                       app_flash_test_stage_name(test_result.stage),
                       (unsigned long)test_result.actual_crc32,
                       (unsigned long)test_result.expected_crc32,
                       (unsigned int)test_result.cleanup_status,
                       (unsigned int)test_result.restored_erased);
        bsp_uart_write_string(BSP_UART_DEBUG, message);
    }
#endif
}
