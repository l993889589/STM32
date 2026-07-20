/**
 * @file main.c
 * @brief ThreadX application composition and CHPM protocol processing.
 *
 * DWIN output is submitted to dwin_tx; only that service owns the UART.
 * Identical screen acknowledgements are link activity, not transaction IDs.
 */

#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_uart.h"
#include "bsp_health.h"
#include "bsp.h"
#include "bsp_led.h"
#include "bsp_reset.h"
#include "bsp_stop.h"
#include "bsp_timebase.h"
#include "bsp_usb.h"
#include "app_health.h"
#include "app_services.h"

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"
#include "ux_device_descriptors.h"
#include "ux_dcd_stm32.h"

#include "ld_modbus_server.h"

#include "app_fan.h"
#include "app_main.h"
#include "app_modbus_param_policy.h"
#include "app_task.h"
#include "bsp_sensor.h"
#include "drv_w25qxx.h"
#include "debug_log.h"
#include "drv_modbus_port.h"
#include "dwin_protocol.h"
#include "param.h"
#include "dwin_ldc_channel.h"
#include "dwin_tx.h"

#define APP_START_PRIORITY       2U
#define APP_COMM_PRIORITY        3U
#define APP_MONITOR_PRIORITY     5U
#define APP_SENSOR_PRIORITY      6U
#define APP_USB_RX_PRIORITY      7U
#define APP_MODBUS_PRIORITY      3U
#define APP_DWIN_RX_PRIORITY     3U
#define APP_PARAM_STORE_PRIORITY 4U

#define APP_THREAD_STACK_SIZE    2048U
#define APP_USBX_POOL_SIZE       10240U
#define APP_USBX_MEMORY_SIZE     4096U
#define APP_PARSER_SIZE          512U
#define APP_USB_IO_TIMEOUT_MS    200U
#define APP_DWIN_OWNER_WAIT_MS   200U

#define APP_MODBUS_BAUD_RATE     115200
#define APP_MODBUS_REGISTER_COUNT 25U
#define APP_MODBUS_COMPAT_UNIT   0xF4U
#define APP_PARAM_STORE_REQUEST  (1UL << 0)
#define APP_PARAM_STORE_COMPLETE (1UL << 1)
#define APP_PARAM_RESPONSE_TIMEOUT_MS (100U)
#define APP_PARAM_PREPARE_IDLE_MS     (50U)
#define APP_PARAM_PREPARE_RETRY_MS    (1000U)
#ifndef APP_LEGACY_RELAY_PB1_ENABLE
#define APP_LEGACY_RELAY_PB1_ENABLE 0U
#endif

#define CPU_TEMP_LIMIT           8000U
#define GPU_TEMP_LIMIT           8000U
#define DS18B20_TEMP_LIMIT       3500U
#define AHT20_TEMP_LIMIT         4200
#define AHT20_HUMIDITY_LIMIT     9000U
#define DS18B20_CONVERSION_TICKS 750U

typedef struct
{
    uint8_t data[APP_PARSER_SIZE];
    uint16_t length;
} app_parser_t;

typedef enum
{
    REG_MODBUS_ADDRESS = 0x0000,
    REG_FAN_MODE = 0x0001,
    REG_FAN_SPEED = 0x0002,
    REG_DISK_STATUS = 0x0003,
    REG_WARNING_STATUS = 0x0004,
    REG_CPU_USAGE = 0x0005,
    REG_CPU_TEMP = 0x0006,
    REG_FAN_RATE = 0x0007,
    REG_CPU_SPEED = 0x0008,
    REG_GPU_USAGE = 0x0009,
    REG_GPU_TEMP = 0x000A,
    REG_GPU_SPEED = 0x000B,
    REG_RAM_USAGE = 0x000C,
    REG_RAM_AVAILABLE = 0x000D,
    REG_MAINBOARD_VOLTAGE = 0x000E,
    REG_DISK_USAGE_1 = 0x000F,
    REG_DISK_USAGE_2 = 0x0010,
    REG_DISK_USAGE_3 = 0x0011,
    REG_DISK_USAGE_4 = 0x0012,
    REG_DISK_USAGE_5 = 0x0013,
    REG_DISK_USAGE_6 = 0x0014,
    REG_DS18B20_TEMP = 0x0015,
    REG_AHT20_TEMP = 0x0016,
    REG_AHT20_HUMIDITY = 0x0017,
    REG_RESTART_COUNT = 0x0018
} app_modbus_register_t;

/** @brief Runtime ownership state of the single static flash request slot. */
typedef enum
{
    APP_PARAM_STORE_STATE_IDLE = 0,
    APP_PARAM_STORE_STATE_REQUESTED,
    APP_PARAM_STORE_STATE_WRITING,
    APP_PARAM_STORE_STATE_COMPLETED,
    APP_PARAM_STORE_STATE_PREPARING
} app_param_store_state_t;

/** @brief Synchronous submission result used by Modbus response policy. */
typedef enum
{
    APP_PARAM_SUBMIT_OK = 0,
    APP_PARAM_SUBMIT_BUSY,
    APP_PARAM_SUBMIT_ACKNOWLEDGED,
    APP_PARAM_SUBMIT_FAILED
} app_param_submit_status_t;

/** @brief Complete static request consumed only by the flash owner thread. */
typedef struct
{
    PARAM_T candidate;
    app_modbus_fan_output_t fan_output;
    uint16_t fan_pwm;
} app_param_store_request_t;

static TX_THREAD app_start_thread;
static TX_THREAD app_comm_thread;
static TX_THREAD app_monitor_thread;
static TX_THREAD app_sensor_thread;
static TX_THREAD app_usb_rx_thread;
static TX_THREAD app_modbus_server_thread;
static TX_THREAD app_dwin_rx_thread;
static TX_THREAD app_param_store_thread;

static uint64_t app_start_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_comm_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_monitor_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_sensor_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_usb_rx_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_modbus_server_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_dwin_rx_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_param_store_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];

static TX_MUTEX app_state_mutex;
static TX_MUTEX app_param_mutex;
static TX_MUTEX app_param_store_gate;
static TX_EVENT_FLAGS_GROUP app_param_store_events;
static app_param_store_request_t app_param_store_request;
static volatile app_param_store_state_t app_param_store_state =
    APP_PARAM_STORE_STATE_IDLE;
static volatile bool app_param_store_waiting;
static volatile app_param_submit_status_t app_param_store_result =
    APP_PARAM_SUBMIT_FAILED;
static volatile param_store_status_t app_param_store_last_result =
    PARAM_STORE_STATUS_OK;
static volatile uint32_t app_param_store_successes;
static volatile uint32_t app_param_store_failures;
static volatile uint32_t app_param_store_busy_rejections;
static volatile uint32_t app_param_store_timeouts;
static volatile uint32_t app_param_spare_prepare_successes;
static volatile uint32_t app_param_spare_prepare_failures;

static TX_BYTE_POOL app_usbx_pool;
static UCHAR app_usbx_pool_buffer[APP_USBX_POOL_SIZE];
static UX_SLAVE_CLASS_CDC_ACM_PARAMETER app_cdc_parameter;
static UX_SLAVE_CLASS_CDC_ACM *volatile app_cdc_instance;

static app_parser_t app_dwin_parser;
static app_parser_t app_usb_parser;
static uint8_t app_modbus_coils[1];
static uint16_t app_modbus_registers[APP_MODBUS_REGISTER_COUNT];
static const ld_modbus_server_map_t app_modbus_mapping =
{
    .coils = app_modbus_coils,
    .coils_start = 0U,
    .coils_count = 1U,
    .discrete_inputs = NULL,
    .discrete_inputs_start = 0U,
    .discrete_inputs_count = 0U,
    .holding_registers = app_modbus_registers,
    .holding_registers_start = 0U,
    .holding_registers_count = APP_MODBUS_REGISTER_COUNT,
    .input_registers = NULL,
    .input_registers_start = 0U,
    .input_registers_count = 0U
};
static bool app_crc_enabled = true;
volatile float ds18b20_temp = -999.0f;
static bool app_temperature_high[3];
static bool app_environment_high[2];
static ULONG app_fan_restore_tick;

static const uint8_t normal_text[] = {0xD5, 0xFD, 0xB3, 0xA3};
static const uint8_t abnormal_text[] = {0xD2, 0xEC, 0xB3, 0xA3};

static const address_mapping_t app_address_map[ADDR_COUNT] =
{
    {0x1320, 0x8800, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1330, 0x8810, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1340, 0x8820, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1350, 0x8830, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1360, 0x8840, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1370, 0x8850, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1100, 0x8860, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1102, 0x8870, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1104, 0x8880, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1106, 0x8890, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1108, 0x88A0, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1160, 0x88C0, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1164, 0x88E0, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)},
    {0x1162, 0x88D0, normal_text, sizeof(normal_text), abnormal_text, sizeof(abnormal_text)}
};

static server_t server;

static void AppTaskStart(ULONG input);
static void app_comm_entry(ULONG input);
static void app_monitor_entry(ULONG input);
static void app_sensor_entry(ULONG input);
static void app_usb_rx_entry(ULONG input);
static void app_modbus_server_entry(ULONG input);
static void app_dwin_rx_entry(ULONG input);
static void app_param_store_entry(ULONG input);
static bool AppTaskCreate(void);
static bool AppObjCreate(void);
static bool app_thread_create(TX_THREAD *thread,
                              CHAR *name,
                              VOID (*entry)(ULONG),
                              VOID *stack,
                              ULONG stack_size,
                              UINT priority);
static UINT app_usb_device_init(TX_BYTE_POOL *pool);
static UINT app_usb_state_change(ULONG state);
static VOID app_usb_activate(VOID *instance);
static VOID app_usb_deactivate(VOID *instance);
static VOID app_usb_parameter_change(VOID *instance);
static void app_parser_feed(app_parser_t *parser, const uint8_t *data, uint16_t length);
static void app_process_frame(uint8_t *frame, uint16_t length);
static void app_process_abcd_frame(const uint8_t *frame, uint16_t length);
static void app_refresh_computer_data(const uint8_t *frame, uint16_t length);
static const address_mapping_t *app_find_mapping(uint16_t address);
static bool app_send_display(uint16_t event_address, const uint8_t *data, size_t length);
static void app_update_temperature_state(void);
static void app_modbus_refresh_registers(void);
static app_param_submit_status_t
    app_modbus_apply_writes_locked(const uint16_t *before);
static uint8_t app_modbus_write_exception(const uint8_t *query, int length);
static bool app_modbus_value_is_valid(uint16_t address, uint16_t value);
static int app_modbus_send(void *user, const uint8_t *data, size_t length);
static void app_modbus_send_exception(const uint8_t *query, int length,
                                      uint8_t exception);
static void app_flash_wait_hook(void);
static ULONG app_timeout_ticks(uint32_t timeout_ms);
static int app_cdc_read(UX_SLAVE_CLASS_CDC_ACM *instance,
                        uint8_t *data,
                        uint32_t length,
                        ULONG *actual_length,
                        uint32_t timeout_ms);
static app_param_submit_status_t app_param_store_submit_locked(
    const PARAM_T *candidate,
    app_modbus_fan_output_t fan_output,
    uint16_t fan_pwm);

/** @brief Send one buffer to the connected USB CDC host. */
int ux_device_cdc_acm_send(uint8_t *data, uint32_t length, uint32_t timeout);

/**
 * @brief Create every application synchronization object.
 * @return True only when every required object exists.
 */
static bool AppObjCreate(void)
{
    if(tx_mutex_create(&app_state_mutex, "state lock", TX_INHERIT) !=
       TX_SUCCESS)
        return false;
    if(tx_mutex_create(&app_param_mutex, "parameter lock", TX_INHERIT) !=
       TX_SUCCESS)
        return false;
    if(tx_mutex_create(&app_param_store_gate,
                       "parameter store gate",
                       TX_INHERIT) != TX_SUCCESS)
        return false;
    return tx_event_flags_create(&app_param_store_events,
                                 "parameter store events") == TX_SUCCESS;
}

/** @brief Create USBX resources and the first ThreadX application thread. */
void tx_application_define(VOID *first_unused_memory)
{
    UINT status;

    (void)first_unused_memory;
    if(!AppObjCreate())
        bsp_stop_on_error();

    status = tx_byte_pool_create(&app_usbx_pool,
                                 "USBX pool",
                                 app_usbx_pool_buffer,
                                 sizeof(app_usbx_pool_buffer));
    if(status != TX_SUCCESS ||
       app_usb_device_init(&app_usbx_pool) != UX_SUCCESS ||
       !app_thread_create(&app_start_thread,
                          "app start",
                          AppTaskStart,
                          app_start_stack,
                          sizeof(app_start_stack),
                          APP_START_PRIORITY))
    {
        bsp_stop_on_error();
    }
}

/** @brief Initialize the BSP and transfer control to ThreadX. */
int main(void)
{
    bsp_status_t status;

    status = bsp_startup();
    if(status != BSP_STATUS_OK)
        bsp_stop_on_error();

    status = bsp_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        bsp_stop_on_error();
    if(bsp_health_init(APP_HEALTH_REQUIRED_SERVICES) != BSP_STATUS_OK)
        bsp_stop_on_error();
    status = bsp_usb_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        bsp_stop_on_error();

    bsp_timebase_suspend();
    tx_kernel_enter();

    while(1)
    {
    }
}

/** @brief Initialize application services and create runtime worker threads. */
static void AppTaskStart(ULONG input)
{
    const app_services_config_t services_config =
    {
        .modbus_baud_rate = APP_MODBUS_BAUD_RATE,
        .tick_1ms_hook = app_server_tick
    };

    (void)input;
    bsp_timebase_resume();
    sf_set_wait_hook(app_flash_wait_hook);
    bsp_InitSFlash();
    ParamLoad();

    app_server_init();
    if(app_services_init(&services_config) != BSP_STATUS_OK)
        Error_Handler();
    if(dwin_tx_init() != DWIN_TX_STATUS_OK)
        Error_Handler();

    app_fan_init();
    (void)bsp_sensor_aht20_reset();
    tx_thread_sleep(20U);
    (void)bsp_sensor_aht20_configure();
    (void)bsp_sensor_ds18b20_probe();
    ResetDiskColorAndEvent();

    if(!AppTaskCreate())
        Error_Handler();

    for(;;)
    {
        bsp_health_poll();
        tx_thread_sleep(1000U);
    }
}

/**
 * @brief Create one static application thread with the shared policy.
 * @return True only when ThreadX accepted the complete thread definition.
 */
static bool app_thread_create(TX_THREAD *thread,
                              CHAR *name,
                              VOID (*entry)(ULONG),
                              VOID *stack,
                              ULONG stack_size,
                              UINT priority)
{
    return tx_thread_create(thread,
                            name,
                            entry,
                            0U,
                            stack,
                            stack_size,
                            priority,
                            priority,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START) == TX_SUCCESS;
}

/**
 * @brief Create all application-owned worker threads.
 * @return True only when every required service thread was created.
 */
static bool AppTaskCreate(void)
{
    /*
     * Create the sole runtime flash owner before any protocol producer.
     * Priority 4 stays below the timing-sensitive UART/DWIN owners at 3, but
     * above continuously active telemetry producers so a save cannot starve.
     */
    return
        app_thread_create(&app_param_store_thread,
                          "parameter flash owner",
                          app_param_store_entry,
                          app_param_store_stack,
                          sizeof(app_param_store_stack),
                          APP_PARAM_STORE_PRIORITY) &&
        app_thread_create(&app_comm_thread,
                          "comm",
                          app_comm_entry,
                          app_comm_stack,
                          sizeof(app_comm_stack),
                          APP_COMM_PRIORITY) &&
        app_thread_create(&app_monitor_thread,
                          "monitor",
                          app_monitor_entry,
                          app_monitor_stack,
                          sizeof(app_monitor_stack),
                          APP_MONITOR_PRIORITY) &&
        app_thread_create(&app_sensor_thread,
                          "sensors",
                          app_sensor_entry,
                          app_sensor_stack,
                          sizeof(app_sensor_stack),
                          APP_SENSOR_PRIORITY) &&
        app_thread_create(&app_usb_rx_thread,
                          "USB RX",
                          app_usb_rx_entry,
                          app_usb_rx_stack,
                          sizeof(app_usb_rx_stack),
                          APP_USB_RX_PRIORITY) &&
        app_thread_create(&app_modbus_server_thread,
                          "ld_modbus server",
                          app_modbus_server_entry,
                          app_modbus_server_stack,
                          sizeof(app_modbus_server_stack),
                          APP_MODBUS_PRIORITY) &&
        app_thread_create(&app_dwin_rx_thread,
                          "DWIN LDC owner",
                          app_dwin_rx_entry,
                          app_dwin_rx_stack,
                          sizeof(app_dwin_rx_stack),
                          APP_DWIN_RX_PRIORITY);
}

/** @brief Refresh the exported Modbus register image from live state. */
static void app_modbus_refresh_registers(void)
{
    VAR_T live;

    /*
     * Copy one coherent telemetry generation. The Modbus thread already owns
     * app_param_mutex for g_tParam while this narrower state lock is held.
     */
    (void)tx_mutex_get(&app_state_mutex, TX_WAIT_FOREVER);
    live = g_tVar;
    (void)tx_mutex_put(&app_state_mutex);

    app_modbus_registers[REG_MODBUS_ADDRESS] = param_rs485_addr_get();
    app_modbus_registers[REG_FAN_MODE] = param_fan_mode_get();
    /*
     * Register 0x0002 is the persistent manual preset.  The live automatic
     * command remains available through runtime telemetry and fan state.
     */
    app_modbus_registers[REG_FAN_SPEED] = g_tParam.pwm_manual;
    app_modbus_registers[REG_DISK_STATUS] = live.DiskInitStatus;
    app_modbus_registers[REG_WARNING_STATUS] = live.DevWarning;
    app_modbus_registers[REG_CPU_USAGE] = live.CpuUsage;
    app_modbus_registers[REG_CPU_TEMP] = live.CpuTemp;
    app_modbus_registers[REG_FAN_RATE] = live.FanRate;
    app_modbus_registers[REG_CPU_SPEED] = live.CpuSpeed;
    app_modbus_registers[REG_GPU_USAGE] = live.GpuUsage;
    app_modbus_registers[REG_GPU_TEMP] = live.GpuTemp;
    app_modbus_registers[REG_GPU_SPEED] = live.GpuSpeed;
    app_modbus_registers[REG_RAM_USAGE] = live.RamUsage;
    app_modbus_registers[REG_RAM_AVAILABLE] = live.RamAvMemory;
    app_modbus_registers[REG_MAINBOARD_VOLTAGE] = live.MbVbatV;
    for(uint8_t i = 0U; i < 6U; i++)
        app_modbus_registers[REG_DISK_USAGE_1 + i] =
            ((live.DiskInitStatus >> i) & 1U) != 0U ?
            live.DiskUsage[i] : 0U;
    app_modbus_registers[REG_DS18B20_TEMP] = live.DS18B20;
    app_modbus_registers[REG_AHT20_TEMP] = live.AHT20TEMP;
    app_modbus_registers[REG_AHT20_HUMIDITY] = live.AHT20HUMI;
    app_modbus_registers[REG_RESTART_COUNT] = g_tParam.RestartCnt;
}

/**
 * @brief Commit one validated Modbus parameter update.
 * @param before Register snapshot captured before Modbus request processing.
 * @return Typed submission result used to select the Modbus response.
 */
static app_param_submit_status_t
app_modbus_apply_writes_locked(const uint16_t *before)
{
    app_modbus_param_registers_t before_config;
    app_modbus_param_registers_t after_config;
    app_modbus_param_update_t update;
    app_param_submit_status_t status;
    uint16_t automatic_pwm;

    before_config.address = before[REG_MODBUS_ADDRESS];
    before_config.fan_mode = before[REG_FAN_MODE];
    before_config.manual_pwm = before[REG_FAN_SPEED];
    after_config.address = app_modbus_registers[REG_MODBUS_ADDRESS];
    after_config.fan_mode = app_modbus_registers[REG_FAN_MODE];
    after_config.manual_pwm = app_modbus_registers[REG_FAN_SPEED];
    (void)tx_mutex_get(&app_state_mutex, TX_WAIT_FOREVER);
    automatic_pwm = g_tVar.pwm_auto;
    (void)tx_mutex_put(&app_state_mutex);
    if(!app_modbus_param_prepare(&g_tParam,
                                  automatic_pwm,
                                  &before_config,
                                  &after_config,
                                  &update))
        return APP_PARAM_SUBMIT_FAILED;
    if(!update.changed)
        return APP_PARAM_SUBMIT_OK;

    /*
     * The flash owner applies RAM and hardware only after the complete
     * candidate is durable.  This also covers a request which finishes after
     * the synchronous Modbus response window has expired.
     */
    status = app_param_store_submit_locked(&update.candidate,
                                            update.fan_output,
                                            update.fan_pwm);
    if(status != APP_PARAM_SUBMIT_OK)
        app_modbus_refresh_registers();
    return status;
}

/** @brief Return the Modbus exception for one candidate write request. */
static uint8_t app_modbus_write_exception(const uint8_t *query, int length)
{
    uint16_t address;
    uint16_t count;
    uint16_t value;

    if(!query || length < 2)
        return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
    if(query[1] == LD_MODBUS_FC_WRITE_SINGLE_COIL)
    {
#if !APP_LEGACY_RELAY_PB1_ENABLE
        return LD_MODBUS_EXCEPTION_SERVER_DEVICE_FAILURE;
#else
        if(length != 8)
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        address = (uint16_t)((query[2] << 8) | query[3]);
        value = (uint16_t)((query[4] << 8) | query[5]);
        if(address != 0U)
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        return (value == 0x0000U || value == 0xFF00U) ? 0U :
               LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
#endif
    }
    if(query[1] == LD_MODBUS_FC_WRITE_MULTIPLE_COILS)
    {
#if !APP_LEGACY_RELAY_PB1_ENABLE
        return LD_MODBUS_EXCEPTION_SERVER_DEVICE_FAILURE;
#else
        if(length != 10 || query[2] != 0U || query[3] != 0U ||
           query[4] != 0U || query[5] != 1U || query[6] != 1U ||
           (query[7] & 0xFEU) != 0U)
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        return 0U;
#endif
    }
    if(query[1] == LD_MODBUS_FC_WRITE_SINGLE_REGISTER)
    {
        if(length != 8)
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        address = (uint16_t)((query[2] << 8) | query[3]);
        value = (uint16_t)((query[4] << 8) | query[5]);
        if(address > REG_FAN_SPEED)
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        return app_modbus_value_is_valid(address, value) ? 0U :
               LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
    }
    if(query[1] == LD_MODBUS_FC_WRITE_MULTIPLE_REGISTERS)
    {
        if(length < 9)
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        address = (uint16_t)((query[2] << 8) | query[3]);
        count = (uint16_t)((query[4] << 8) | query[5]);
        if(count == 0U || address > REG_FAN_SPEED ||
           (uint32_t)address + count > REG_FAN_SPEED + 1U ||
           query[6] != count * 2U || length != (int)(9U + count * 2U))
            return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        for(uint16_t i = 0U; i < count; i++)
        {
            value = (uint16_t)((query[7U + i * 2U] << 8) | query[8U + i * 2U]);
            if(!app_modbus_value_is_valid((uint16_t)(address + i), value))
                return LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        }
        return 0U;
    }
    if(query[1] == LD_MODBUS_FC_MASK_WRITE_REGISTER ||
       query[1] == LD_MODBUS_FC_WRITE_READ_MULTIPLE_REGISTERS)
        return LD_MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
    return 0U;
}

/** @brief Validate one writable Modbus register value. */
static bool app_modbus_value_is_valid(uint16_t address, uint16_t value)
{
    if(address == REG_MODBUS_ADDRESS)
        return value >= 1U && value <= 247U;
    if(address == REG_FAN_MODE)
        return value <= 1U;
    if(address == REG_FAN_SPEED)
        return value >= 4000U && value <= 10000U;
    return false;
}

/** @brief Transmit one Modbus RTU response through the BSP port. */
static int app_modbus_send(void *user, const uint8_t *data, size_t length)
{
    (void)user;
    if(length > UINT16_MAX)
        return -1;
    return drv_modbus_port_write(data, (uint16_t)length, 100U) == BSP_STATUS_OK ?
           (int)length : -1;
}

/** @brief Build and send a Modbus exception response. */
static void app_modbus_send_exception(const uint8_t *query, int length,
                                      uint8_t exception)
{
    ld_modbus_adu_view_t view;
    uint8_t pdu[2];
    uint8_t response[5];
    size_t response_length;

    if(query == NULL || length <= 0 ||
       ld_modbus_rtu_decode(query, (size_t)length, &view) != LD_MODBUS_STATUS_OK ||
       view.unit_id == LD_MODBUS_BROADCAST_UNIT_ID ||
       view.pdu_length == 0U)
        return;
    pdu[0] = (uint8_t)(view.pdu[0] | 0x80U);
    pdu[1] = exception;
    /*
     * Echo the already validated request address.  The durable commit may
     * change g_tParam.Addr485 before a late 0x05 response is constructed.
     */
    if(ld_modbus_rtu_encode(view.unit_id, pdu, sizeof(pdu),
                            response, sizeof(response), &response_length) ==
       LD_MODBUS_STATUS_OK)
        (void)app_modbus_send(NULL, response, response_length);
}

/** @brief Yield one ThreadX tick between W25Q64 busy-status polls. */
static void app_flash_wait_hook(void)
{
    tx_thread_sleep(1U);
}

/**
 * @brief Convert a physical timeout to at least one ThreadX timer tick.
 * @param timeout_ms Timeout in milliseconds; zero selects a non-blocking wait.
 * @return ThreadX tick count suitable for event and mutex waits.
 */
static ULONG app_timeout_ticks(uint32_t timeout_ms)
{
    uint64_t ticks;

    if(timeout_ms == 0U)
        return TX_NO_WAIT;
    ticks = ((uint64_t)timeout_ms * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U;
    if(ticks == 0U)
        ticks = 1U;
    return ticks > UINT32_MAX ? (ULONG)UINT32_MAX : (ULONG)ticks;
}

/**
 * @brief Submit one serialized flash request while holding app_param_mutex.
 * @param candidate Complete validated parameter snapshot.
 * @param fan_output Hardware action to apply after a durable commit.
 * @param fan_pwm PWM value associated with @p fan_output.
 * @return Typed completion, busy, acknowledged, or failure result.
 */
static app_param_submit_status_t app_param_store_submit_locked(
    const PARAM_T *candidate,
    app_modbus_fan_output_t fan_output,
    uint16_t fan_pwm)
{
    ULONG actual_flags;
    app_param_submit_status_t result;
    UINT status;

    if(candidate == NULL)
        return APP_PARAM_SUBMIT_FAILED;
    if(tx_mutex_get(&app_param_store_gate, TX_NO_WAIT) != TX_SUCCESS)
    {
        app_param_store_busy_rejections++;
        return APP_PARAM_SUBMIT_BUSY;
    }
    if(app_param_store_state != APP_PARAM_STORE_STATE_IDLE)
    {
        app_param_store_busy_rejections++;
        (void)tx_mutex_put(&app_param_store_gate);
        return APP_PARAM_SUBMIT_BUSY;
    }

    /* Discard a stale completion before publishing the single owned request. */
    (void)tx_event_flags_get(&app_param_store_events,
                             APP_PARAM_STORE_COMPLETE,
                             TX_OR_CLEAR,
                             &actual_flags,
                             TX_NO_WAIT);
    app_param_store_request.candidate = *candidate;
    app_param_store_request.fan_output = fan_output;
    app_param_store_request.fan_pwm = fan_pwm;
    app_param_store_result = APP_PARAM_SUBMIT_FAILED;
    app_param_store_waiting = true;
    app_param_store_state = APP_PARAM_STORE_STATE_REQUESTED;
    if(tx_event_flags_set(&app_param_store_events,
                          APP_PARAM_STORE_REQUEST,
                          TX_OR) != TX_SUCCESS)
    {
        app_param_store_waiting = false;
        app_param_store_state = APP_PARAM_STORE_STATE_IDLE;
        (void)tx_mutex_put(&app_param_store_gate);
        return APP_PARAM_SUBMIT_FAILED;
    }
    (void)tx_mutex_put(&app_param_store_gate);

    /*
     * The request remains owned by the flash thread after a response timeout.
     * Its non-idle state prevents another caller from overwriting the slot.
     */
    status = tx_event_flags_get(&app_param_store_events,
                                APP_PARAM_STORE_COMPLETE,
                                TX_OR_CLEAR,
                                &actual_flags,
                                app_timeout_ticks(APP_PARAM_RESPONSE_TIMEOUT_MS));
    if(status == TX_SUCCESS)
    {
        if(tx_mutex_get(&app_param_store_gate, TX_WAIT_FOREVER) != TX_SUCCESS)
            return APP_PARAM_SUBMIT_FAILED;
        result = app_param_store_result;
        app_param_store_waiting = false;
        if(app_param_store_state == APP_PARAM_STORE_STATE_COMPLETED)
            app_param_store_state = APP_PARAM_STORE_STATE_IDLE;
        (void)tx_mutex_put(&app_param_store_gate);
        return result;
    }

    /*
     * Close the event-at-deadline race before reporting that the accepted
     * operation is still in progress.
     */
    if(tx_event_flags_get(&app_param_store_events,
                          APP_PARAM_STORE_COMPLETE,
                          TX_OR_CLEAR,
                          &actual_flags,
                          TX_NO_WAIT) == TX_SUCCESS)
    {
        if(tx_mutex_get(&app_param_store_gate, TX_WAIT_FOREVER) != TX_SUCCESS)
            return APP_PARAM_SUBMIT_FAILED;
        result = app_param_store_result;
        app_param_store_waiting = false;
        if(app_param_store_state == APP_PARAM_STORE_STATE_COMPLETED)
            app_param_store_state = APP_PARAM_STORE_STATE_IDLE;
        (void)tx_mutex_put(&app_param_store_gate);
        return result;
    }

    /*
     * If the owner crossed the deadline just before this gate acquisition,
     * consume its result instead of incorrectly reporting 0x05.
     */
    if(tx_mutex_get(&app_param_store_gate, TX_WAIT_FOREVER) != TX_SUCCESS)
        return APP_PARAM_SUBMIT_FAILED;
    if(app_param_store_state == APP_PARAM_STORE_STATE_COMPLETED)
    {
        result = app_param_store_result;
        app_param_store_waiting = false;
        app_param_store_state = APP_PARAM_STORE_STATE_IDLE;
        (void)tx_event_flags_get(&app_param_store_events,
                                 APP_PARAM_STORE_COMPLETE,
                                 TX_OR_CLEAR,
                                 &actual_flags,
                                 TX_NO_WAIT);
        (void)tx_mutex_put(&app_param_store_gate);
        return result;
    }
    app_param_store_waiting = false;
    (void)tx_mutex_put(&app_param_store_gate);
    app_param_store_timeouts++;
    return APP_PARAM_SUBMIT_ACKNOWLEDGED;
}

/** @brief Own every runtime W25Q64 parameter erase/program transaction. */
static void app_param_store_entry(ULONG input)
{
    ULONG actual_flags;
    ULONG next_prepare_tick = 0U;

    (void)input;
    for(;;)
    {
        app_param_store_request_t request;
        param_store_status_t store_status;
        UINT wait_status;

        bsp_health_heartbeat(APP_HEALTH_SERVICE_PARAM_STORE);
        wait_status = tx_event_flags_get(&app_param_store_events,
                                         APP_PARAM_STORE_REQUEST,
                                         TX_OR_CLEAR,
                                         &actual_flags,
                                         app_timeout_ticks(
                                             APP_PARAM_PREPARE_IDLE_MS));
        if(wait_status == TX_SUCCESS)
        {
            if(tx_mutex_get(&app_param_store_gate, TX_WAIT_FOREVER) !=
               TX_SUCCESS)
                continue;
            if(app_param_store_state != APP_PARAM_STORE_STATE_REQUESTED)
            {
                (void)tx_mutex_put(&app_param_store_gate);
                continue;
            }
            app_param_store_state = APP_PARAM_STORE_STATE_WRITING;
            request = app_param_store_request;
            (void)tx_mutex_put(&app_param_store_gate);

            store_status = ParamCommit(&request.candidate);
            if(store_status == PARAM_STORE_STATUS_OK)
            {
                /*
                 * Hardware follows only the snapshot which passed flash
                 * read-back and commit-marker verification.
                 */
                acquire_mutex();
                ParamPublishRuntime();
                release_mutex();
                if(request.fan_output == APP_MODBUS_FAN_OUTPUT_MANUAL)
                    app_fan_set_duty(request.fan_pwm);
                else if(request.fan_output == APP_MODBUS_FAN_OUTPUT_AUTO)
                    app_fan_set_duty_by_auto(request.fan_pwm);
                app_param_store_result = APP_PARAM_SUBMIT_OK;
                app_param_store_successes++;
            }
            else if(store_status == PARAM_STORE_STATUS_SPARE_NOT_READY)
            {
                app_param_store_result = APP_PARAM_SUBMIT_BUSY;
                app_param_store_busy_rejections++;
            }
            else
            {
                app_param_store_result = APP_PARAM_SUBMIT_FAILED;
                app_param_store_failures++;
            }
            /*
             * Keep a completed request owned until its synchronous waiter
             * consumes the result.  If that waiter already returned 0x05, the
             * owner releases the slot immediately after finishing.
             */
            if(tx_mutex_get(&app_param_store_gate, TX_WAIT_FOREVER) ==
               TX_SUCCESS)
            {
                app_param_store_last_result = store_status;
                if(app_param_store_waiting)
                {
                    app_param_store_state =
                        APP_PARAM_STORE_STATE_COMPLETED;
                    (void)tx_event_flags_set(&app_param_store_events,
                                             APP_PARAM_STORE_COMPLETE,
                                             TX_OR);
                }
                else
                {
                    app_param_store_state = APP_PARAM_STORE_STATE_IDLE;
                }
                (void)tx_mutex_put(&app_param_store_gate);
            }
            continue;
        }

        /*
         * Erase the consumed journal sector only while the owner is idle.
         * A request arriving during this bounded operation receives 0x06 and
         * can be retried without ever sharing the SPI flash.
         */
        if(ParamSpareNeedsErase() &&
           (LONG)(tx_time_get() - next_prepare_tick) >= 0)
        {
            if(tx_mutex_get(&app_param_store_gate, TX_NO_WAIT) != TX_SUCCESS)
                continue;
            if(app_param_store_state != APP_PARAM_STORE_STATE_IDLE)
            {
                (void)tx_mutex_put(&app_param_store_gate);
                continue;
            }
            app_param_store_state = APP_PARAM_STORE_STATE_PREPARING;
            (void)tx_mutex_put(&app_param_store_gate);

            store_status = ParamPrepareSpare();
            app_param_store_last_result = store_status;
            if(store_status == PARAM_STORE_STATUS_OK)
                app_param_spare_prepare_successes++;
            else
                app_param_spare_prepare_failures++;

            if(tx_mutex_get(&app_param_store_gate, TX_WAIT_FOREVER) ==
               TX_SUCCESS)
            {
                app_param_store_state = APP_PARAM_STORE_STATE_IDLE;
                (void)tx_mutex_put(&app_param_store_gate);
            }
            next_prepare_tick = tx_time_get() +
                app_timeout_ticks(store_status == PARAM_STORE_STATUS_OK ?
                                  APP_PARAM_PREPARE_IDLE_MS :
                                  APP_PARAM_PREPARE_RETRY_MS);
        }
    }
}

/** @brief Own the Modbus RTU server receive/process loop. */
static void app_modbus_server_entry(ULONG input)
{
    uint8_t query[LD_MODBUS_RTU_MAX_ADU_LENGTH];
    uint8_t response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
    uint16_t before[APP_MODBUS_REGISTER_COUNT];
    ld_modbus_status_t status;

    (void)input;

    for(;;)
    {
        ld_modbus_adu_view_t view;
        ld_modbus_server_action_t action;
        uint8_t exception;
        size_t response_length;
        app_param_submit_status_t apply_status = APP_PARAM_SUBMIT_OK;

        bsp_health_heartbeat(APP_HEALTH_SERVICE_MODBUS);
        int length = drv_modbus_port_read_frame(query, sizeof(query), 100U);

        if(length <= 0)
            continue;
        status = ld_modbus_rtu_decode(query, (size_t)length, &view);
        if(status != LD_MODBUS_STATUS_OK)
            continue;
        if(view.unit_id != LD_MODBUS_BROADCAST_UNIT_ID &&
           view.unit_id != param_rs485_addr_get() &&
           view.unit_id != APP_MODBUS_COMPAT_UNIT)
            continue;
        exception = app_modbus_write_exception(query, length);
        if(exception != 0U)
        {
            app_modbus_send_exception(query, length, exception);
            continue;
        }
        if(view.unit_id == APP_MODBUS_COMPAT_UNIT)
        {
            uint16_t crc;
            query[0] = param_rs485_addr_get();
            crc = ld_modbus_crc16(query, (size_t)length - 2U);
            query[length - 2] = (uint8_t)crc;
            query[length - 1] = (uint8_t)(crc >> 8);
        }
        if(tx_mutex_get(&app_param_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
            continue;
        app_modbus_refresh_registers();
        memcpy(before, app_modbus_registers, sizeof(before));
        status = ld_modbus_server_process_rtu_adu(&app_modbus_mapping,
                                                  param_rs485_addr_get(),
                                                  query,
                                                  (size_t)length,
                                                  response,
                                                  sizeof(response),
                                                  &response_length,
                                                  &action);
        if(status == LD_MODBUS_STATUS_OK &&
           action != LD_MODBUS_SERVER_ACTION_IGNORED)
            apply_status = app_modbus_apply_writes_locked(before);
        (void)tx_mutex_put(&app_param_mutex);
        if(status == LD_MODBUS_STATUS_OK &&
           action != LD_MODBUS_SERVER_ACTION_IGNORED &&
           action == LD_MODBUS_SERVER_ACTION_REPLY)
        {
            if(apply_status == APP_PARAM_SUBMIT_OK)
                (void)app_modbus_send(NULL, response, response_length);
            else if(apply_status == APP_PARAM_SUBMIT_BUSY)
                app_modbus_send_exception(query, length,
                    LD_MODBUS_EXCEPTION_SERVER_DEVICE_BUSY);
            else if(apply_status == APP_PARAM_SUBMIT_ACKNOWLEDGED)
                app_modbus_send_exception(query, length,
                    LD_MODBUS_EXCEPTION_ACKNOWLEDGE);
            else
                app_modbus_send_exception(query, length,
                    LD_MODBUS_EXCEPTION_SERVER_DEVICE_FAILURE);
        }
    }
}

/** @brief Own DWIN receive parsing and fixed-ACK publication. */
static void app_dwin_rx_entry(ULONG input)
{
    (void)input;
    for(;;)
    {
        bsp_health_heartbeat(APP_HEALTH_SERVICE_DWIN_RX);
        dwin_ldc_channel_owner_wait(
            app_timeout_ticks(APP_DWIN_OWNER_WAIT_MS));
    }
}

/** @brief Run the application service one-millisecond timebase. */
static void app_comm_entry(ULONG input)
{
    (void)input;

    for(;;)
    {
        bsp_health_heartbeat(APP_HEALTH_SERVICE_COMM);
        app_services_tick_1ms();
        tx_thread_sleep(1U);
    }
}

/** @brief Sample sensors and submit their newest DWIN values. */
static void app_sensor_entry(ULONG input)
{
    ULONG next_aht = 0U;
    ULONG next_ds = 0U;
    ULONG ds_ready = 0U;
    bool ds_pending = false;
    uint8_t page = 1U;

    (void)input;
    for(;;)
    {
        ULONG now = tx_time_get();

        bsp_health_heartbeat(APP_HEALTH_SERVICE_SENSOR);
        if((LONG)(now - next_aht) >= 0)
        {
            aht20_measurement_t measurement;

            if(bsp_sensor_aht20_start_measurement() == SENSOR_STATUS_OK)
            {
                tx_thread_sleep(100U);
                if(bsp_sensor_aht20_read_measurement(&measurement) ==
                   SENSOR_STATUS_OK)
                {
                    uint8_t payload[8];
                    float temperature = measurement.temperature_c;
                    float humidity = measurement.humidity_percent;
                    int16_t temperature_scaled = (int16_t)(temperature * 100.0f);
                    uint16_t humidity_scaled = (uint16_t)(humidity * 100.0f);
                    uint32_t raw;

                    acquire_mutex();
                    SetGlobalVar(AHT20TEMP, &temperature_scaled);
                    SetGlobalVar(AHT20HUMI, &humidity_scaled);
                    release_mutex();
                    update_environmental_params(temperature, humidity);
                    memcpy(&raw, &temperature, sizeof(raw));
                    payload[0] = (uint8_t)(raw >> 24);
                    payload[1] = (uint8_t)(raw >> 16);
                    payload[2] = (uint8_t)(raw >> 8);
                    payload[3] = (uint8_t)raw;
                    memcpy(&raw, &humidity, sizeof(raw));
                    payload[4] = (uint8_t)(raw >> 24);
                    payload[5] = (uint8_t)(raw >> 16);
                    payload[6] = (uint8_t)(raw >> 8);
                    payload[7] = (uint8_t)raw;
                    (void)dwin_tx_submit_write_latest(0x1162U,
                                                      payload,
                                                      sizeof(payload));

                    if((temperature_scaled > AHT20_TEMP_LIMIT) != app_environment_high[0])
                    {
                        app_environment_high[0] = temperature_scaled > AHT20_TEMP_LIMIT;
                        (void)send_alarm_color_packet(0x88D3U,
                            app_environment_high[0] ? 0xF800U : 0xFFFFU);
                    }
                    if((humidity_scaled > AHT20_HUMIDITY_LIMIT) != app_environment_high[1])
                    {
                        app_environment_high[1] = humidity_scaled > AHT20_HUMIDITY_LIMIT;
                        (void)send_alarm_color_packet(0x88E3U,
                            app_environment_high[1] ? 0xF800U : 0xFFFFU);
                    }
                }
            }
            (void)send_page_switch_command(page);
            page = page >= 2U ? 1U : (uint8_t)(page + 1U);
            next_aht = tx_time_get() + 10000U;
        }

        now = tx_time_get();
        if(!ds_pending && (LONG)(now - next_ds) >= 0)
        {
            if(bsp_sensor_ds18b20_start_conversion() == SENSOR_STATUS_OK)
            {
                ds_pending = true;
                ds_ready = now + DS18B20_CONVERSION_TICKS;
            }
            else
            {
                next_ds = now + 10000U;
            }
        }
        if(ds_pending && (LONG)(now - ds_ready) >= 0)
        {
            float temperature;

            if(bsp_sensor_ds18b20_read_temperature(&temperature) ==
               SENSOR_STATUS_OK)
            {
                uint8_t payload[4];
                int16_t scaled = (int16_t)(temperature * 100.0f);
                uint32_t raw;

                acquire_mutex();
                ds18b20_temp = temperature;
                SetGlobalVar(DS18B20, &scaled);
                release_mutex();
                memcpy(&raw, &temperature, sizeof(raw));
                payload[0] = (uint8_t)(raw >> 24);
                payload[1] = (uint8_t)(raw >> 16);
                payload[2] = (uint8_t)(raw >> 8);
                payload[3] = (uint8_t)raw;
                (void)dwin_tx_submit_write_latest(0x1160U,
                                                  payload,
                                                  sizeof(payload));
            }
            ds_pending = false;
            next_ds = now + 10000U;
        }

        tx_thread_sleep(50U);
    }
}

/** @brief Run fan, heartbeat, alarm-state, and status LED maintenance. */
static void app_monitor_entry(ULONG input)
{
    ULONG next_slow = 0U;
    ULONG next_led = 0U;

    (void)input;
    for(;;)
    {
        ULONG now = tx_time_get();

        bsp_health_heartbeat(APP_HEALTH_SERVICE_MONITOR);
        app_fan_process();

        if((LONG)(now - next_slow) >= 0)
        {
            check_server_ping();
            app_update_temperature_state();
            next_slow = now + 1000U;
        }

        if((LONG)(now - next_led) >= 0)
        {
            bsp_led_toggle(BSP_LED_STATUS);
            next_led = now + (get_connect_state() ? 1000U : 200U);
        }

        tx_thread_sleep(20U);
    }
}

/** @brief Receive USB CDC bytes and feed the host protocol parser. */
static void app_usb_rx_entry(ULONG input)
{
    UCHAR data[64];
    ULONG actual_length;

    (void)input;
    for(;;)
    {
        UX_SLAVE_CLASS_CDC_ACM *instance = app_cdc_instance;

        bsp_health_heartbeat(APP_HEALTH_SERVICE_USB_RX);
        if(!instance)
        {
            tx_thread_sleep(20U);
            continue;
        }

        actual_length = 0U;
        if(app_cdc_read(instance,
                        data,
                        sizeof(data),
                        &actual_length,
                        APP_USB_IO_TIMEOUT_MS) == 0 &&
           actual_length > 0U)
        {
            usb_parse_crc(data, (int)actual_length);
        }
    }
}

/** @brief Initialize USBX device stack and register the CDC ACM class. */
static UINT app_usb_device_init(TX_BYTE_POOL *pool)
{
    UCHAR *memory;
    UCHAR *high_speed;
    UCHAR *full_speed;
    UCHAR *strings;
    UCHAR *languages;
    ULONG high_length;
    ULONG full_length;
    ULONG string_length;
    ULONG language_length;

    if(tx_byte_allocate(pool, (VOID **)&memory, APP_USBX_MEMORY_SIZE, TX_NO_WAIT) != TX_SUCCESS)
        return TX_POOL_ERROR;
    if(ux_system_initialize(memory, APP_USBX_MEMORY_SIZE, UX_NULL, 0U) != UX_SUCCESS)
        return UX_ERROR;

    high_speed = USBD_Get_Device_Framework_Speed(USBD_HIGH_SPEED, &high_length);
    full_speed = USBD_Get_Device_Framework_Speed(USBD_FULL_SPEED, &full_length);
    strings = USBD_Get_String_Framework(&string_length);
    languages = USBD_Get_Language_Id_Framework(&language_length);

    if(ux_device_stack_initialize(high_speed,
                                  high_length,
                                  full_speed,
                                  full_length,
                                  strings,
                                  string_length,
                                  languages,
                                  language_length,
                                  app_usb_state_change) != UX_SUCCESS)
        return UX_ERROR;

    memset(&app_cdc_parameter, 0, sizeof(app_cdc_parameter));
    app_cdc_parameter.ux_slave_class_cdc_acm_instance_activate = app_usb_activate;
    app_cdc_parameter.ux_slave_class_cdc_acm_instance_deactivate = app_usb_deactivate;
    app_cdc_parameter.ux_slave_class_cdc_acm_parameter_change = app_usb_parameter_change;

    if(ux_device_stack_class_register(_ux_system_slave_class_cdc_acm_name,
                                      ux_device_class_cdc_acm_entry,
                                      1U,
                                      0U,
                                      &app_cdc_parameter) != UX_SUCCESS)
        return UX_ERROR;

    if(ux_dcd_stm32_initialize(0U,
                               (ULONG)bsp_usb_get_dcd_context()) != UX_SUCCESS)
        return UX_ERROR;

    if(bsp_usb_start() != BSP_STATUS_OK)
        return UX_ERROR;
    return UX_SUCCESS;
}

/** @brief Accept USBX device state change notifications. */
static UINT app_usb_state_change(ULONG state)
{
    (void)state;
    return UX_SUCCESS;
}

/** @brief Publish the active USB CDC instance. */
static VOID app_usb_activate(VOID *instance)
{
    app_cdc_instance = (UX_SLAVE_CLASS_CDC_ACM *)instance;
}

/** @brief Clear the USB CDC instance when the host disconnects. */
static VOID app_usb_deactivate(VOID *instance)
{
    if(app_cdc_instance == instance)
        app_cdc_instance = UX_NULL;
}

/** @brief Accept CDC line-coding changes without altering the virtual link. */
static VOID app_usb_parameter_change(VOID *instance)
{
    (void)instance;
}

/** @brief Send an entire application buffer through USB CDC. */
int ux_device_cdc_acm_send(uint8_t *data, uint32_t length, uint32_t timeout)
{
    ULONG actual_length = 0U;
    UX_SLAVE_CLASS_CDC_ACM *instance = app_cdc_instance;

    if(!instance || !data || length == 0U ||
       ux_device_class_cdc_acm_ioctl(
           instance,
           UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_WRITE_TIMEOUT,
           (VOID *)(ALIGN_TYPE)app_timeout_ticks(timeout)) != UX_SUCCESS)
    {
        return -1;
    }
    return ux_device_class_cdc_acm_write(instance,
                                         data,
                                         length,
                                         &actual_length) == UX_SUCCESS &&
           actual_length == length ? 0 : -1;
}

/**
 * @brief Perform one bounded USB CDC read through the class timeout contract.
 * @return Zero on a successful USBX read, otherwise -1.
 */
static int app_cdc_read(UX_SLAVE_CLASS_CDC_ACM *instance,
                        uint8_t *data,
                        uint32_t length,
                        ULONG *actual_length,
                        uint32_t timeout_ms)
{
    if(instance == NULL || data == NULL || length == 0U ||
       actual_length == NULL)
    {
        return -1;
    }
    if(ux_device_class_cdc_acm_ioctl(
           instance,
           UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_READ_TIMEOUT,
           (VOID *)(ALIGN_TYPE)app_timeout_ticks(timeout_ms)) != UX_SUCCESS)
    {
        return -1;
    }
    return ux_device_class_cdc_acm_read(instance,
                                        data,
                                        length,
                                        actual_length) == UX_SUCCESS ?
           0 : -1;
}

/** @brief Read one byte from USB CDC for compatibility callers. */
int ux_device_cdc_acm_getchar(uint8_t *data, uint32_t timeout)
{
    ULONG actual_length = 0U;
    UX_SLAVE_CLASS_CDC_ACM *instance = app_cdc_instance;

    if(!instance || !data)
        return -1;
    return app_cdc_read(instance, data, 1U, &actual_length, timeout) == 0 &&
           actual_length == 1U ? 0 : -1;
}

/** @brief Preserve the legacy CDC flush API; USBX needs no explicit flush. */
int ux_device_cdc_acm_flush(void)
{
    return 0;
}

/**
 * @brief Validate and synchronously persist one parameter snapshot.
 * @return true only when the flash owner committed and published the snapshot.
 */
bool ParamQueueSend(PARAM_T param)
{
    app_param_submit_status_t status;

    if(param.Addr485 < 1U || param.Addr485 > 247U || param.mode > 1U ||
       param.pwm_manual < 4000U || param.pwm_manual > 10000U ||
       param.pwm_auto < 4000U || param.pwm_auto > 10000U)
        return false;

    if(tx_mutex_get(&app_param_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        return false;
    status = app_param_store_submit_locked(&param,
                                            APP_MODBUS_FAN_OUTPUT_NONE,
                                            0U);
    (void)tx_mutex_put(&app_param_mutex);
    return status == APP_PARAM_SUBMIT_OK;
}

/**
 * @brief Copy the parameter persistence health snapshot for diagnostics.
 * @param health Destination structure; ignored when NULL.
 */
void app_param_store_get_health(app_param_store_health_t *health)
{
    if(health == NULL)
        return;

    /*
     * Individual counters are naturally aligned MCU words.  This diagnostic
     * snapshot is intentionally approximate and never blocks the flash owner.
     */
    health->success_count = app_param_store_successes;
    health->failure_count = app_param_store_failures;
    health->busy_count = app_param_store_busy_rejections;
    health->timeout_count = app_param_store_timeouts;
    health->spare_prepare_success_count =
        app_param_spare_prepare_successes;
    health->spare_prepare_failure_count =
        app_param_spare_prepare_failures;
    health->last_result = app_param_store_last_result;
    health->busy =
        app_param_store_state == APP_PARAM_STORE_STATE_IDLE ? 0U : 1U;
}

/** @brief Latch the reliable periodic DWIN buzzer on. */
void app_set_alarm(void)
{
    (void)dwin_tx_set_buzzer(true);
}

/** @brief Cancel the periodic DWIN buzzer schedule. */
void app_set_alarm_stop(void)
{
    (void)dwin_tx_set_buzzer(false);
}

/** @brief Submit one ordered DWIN color update. */
bool send_alarm_color_packet(uint16_t address, uint16_t color)
{
    uint8_t payload[2] = {(uint8_t)(color >> 8), (uint8_t)color};

    return dwin_tx_submit_write_event(address,
                                      payload,
                                      sizeof(payload),
                                      2U) == DWIN_TX_STATUS_OK;
}

/** @brief Submit one ordered DWIN page switch. */
bool send_page_switch_command(uint8_t page)
{
    uint8_t payload[4] = {0x5AU, 0x01U, 0x00U, page};

    return dwin_tx_submit_write_event(0x0084U,
                                      payload,
                                      sizeof(payload),
                                      2U) == DWIN_TX_STATUS_OK;
}

/** @brief Compatibility alias for the standard page switch command. */
bool send_page_switch_command_ex(uint8_t page)
{
    return send_page_switch_command(page);
}

/** @brief Find the display mapping for one host event address. */
static const address_mapping_t *app_find_mapping(uint16_t address)
{
    for(size_t i = 0U; i < ADDR_COUNT; i++)
    {
        if(app_address_map[i].initial_addr == address)
            return &app_address_map[i];
    }
    return NULL;
}

/** @brief Submit mapped display text as an ordered DWIN event. */
static bool app_send_display(uint16_t event_address, const uint8_t *data, size_t length)
{
    const address_mapping_t *mapping = app_find_mapping(event_address);

    if(!mapping || length > 20U)
        return false;
    return dwin_tx_submit_write_event(mapping->corresponding_addr,
                                      data,
                                      (uint16_t)length,
                                      2U) == DWIN_TX_STATUS_OK;
}

/** @brief Submit a mapped alarm color using the event presentation offset. */
bool send_alarm_color_packet_with_offset(uint16_t initial_address,
                                         uint16_t color,
                                         uint8_t event_type)
{
    const address_mapping_t *mapping = app_find_mapping(initial_address);

    if(!mapping)
        return false;
    if(event_type == 0x01U)
        color = 0xFFE0U;
    else if(event_type == 0x03U)
        color = 0xF800U;
    return send_alarm_color_packet((uint16_t)(mapping->corresponding_addr + 3U), color);
}

/** @brief Decode one big-endian IEEE-754 float from host data. */
static float app_read_be_float(const uint8_t *data)
{
    uint32_t raw = ((uint32_t)data[0] << 24) |
                   ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) |
                   data[3];
    float value;

    memcpy(&value, &raw, sizeof(value));
    return value;
}

/** @brief Convert a non-negative float to a saturated hundredths value. */
static uint16_t app_scale_float(float value)
{
    if(value != value)
        return 0U;
    if(value <= 0.0f)
        return 0U;
    if(value >= 655.35f)
        return UINT16_MAX;
    return (uint16_t)(value * 100.0f);
}

/** @brief Decode live computer telemetry into the shared application state. */
static void app_refresh_computer_data(const uint8_t *frame, uint16_t length)
{
    uint16_t value;
    uint32_t fan_rate;
    size_t disk_offset = 46U;
    static const uint8_t invalid_gpu[4] = {0xBFU, 0x80U, 0x00U, 0x00U};

    if(length < 46U)
        return;

    acquire_mutex();
    value = app_scale_float(app_read_be_float(frame + 6U));
    SetGlobalVar(VAR_CPU_USAGE, &value);
    value = app_scale_float(app_read_be_float(frame + 10U));
    SetGlobalVar(VAR_CPU_TEMP, &value);
    fan_rate = ((uint32_t)frame[14] << 24) | ((uint32_t)frame[15] << 16) |
               ((uint32_t)frame[16] << 8) | frame[17];
    value = fan_rate > UINT16_MAX ? UINT16_MAX : (uint16_t)fan_rate;
    SetGlobalVar(VAR_FAN_RATE, &value);
    value = app_scale_float(app_read_be_float(frame + 18U));
    SetGlobalVar(VAR_CPU_SPEED, &value);

    if(memcmp(frame + 22U, invalid_gpu, sizeof(invalid_gpu)) == 0 &&
       memcmp(frame + 26U, invalid_gpu, sizeof(invalid_gpu)) == 0 &&
       memcmp(frame + 30U, invalid_gpu, sizeof(invalid_gpu)) == 0)
    {
        value = 0U;
        SetDiskInitStatusBit(6U, 0U);
        SetGlobalVar(VAR_GPU_USAGE, &value);
        SetGlobalVar(VAR_GPU_TEMP, &value);
        SetGlobalVar(VAR_GPU_SPEED, &value);
    }
    else
    {
        SetDiskInitStatusBit(6U, 1U);
        value = app_scale_float(app_read_be_float(frame + 22U));
        SetGlobalVar(VAR_GPU_USAGE, &value);
        value = app_scale_float(app_read_be_float(frame + 26U));
        SetGlobalVar(VAR_GPU_TEMP, &value);
        value = app_scale_float(app_read_be_float(frame + 30U));
        SetGlobalVar(VAR_GPU_SPEED, &value);
    }

    value = app_scale_float(app_read_be_float(frame + 34U));
    SetGlobalVar(VAR_RAM_USAGE, &value);
    value = app_scale_float(app_read_be_float(frame + 38U));
    SetGlobalVar(VAR_RAM_AVAIL_MEMORY, &value);
    value = app_scale_float(app_read_be_float(frame + 42U));
    SetGlobalVar(VAR_MB_VBAT_VOLTAGE, &value);

    for(uint8_t disk = 0U; disk < 6U; disk++)
    {
        if(GetDiskInitStatusBit(disk))
        {
            if(disk_offset + 4U > length)
                break;
            value = app_scale_float(app_read_be_float(frame + disk_offset));
            SetGlobalVar((VarType)(VAR_DISK_USAGE_1 + disk), &value);
            disk_offset += 4U;
        }
    }
    release_mutex();
}

/** @brief Route one validated 5A A5 host frame to DWIN policy and state. */
static void app_process_frame(uint8_t *frame, uint16_t length)
{
    uint16_t address;

    if(!frame || length < 6U)
        return;
    address = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);

    if(address == 0x1100U)
    {
        app_refresh_computer_data(frame, length);
        (void)dwin_tx_submit_raw_latest(address, frame, length);
    }
    else if(address == 0x0004U)
    {
        static const uint8_t reset_command[] =
            {0x5AU, 0xA5U, 0x09U, 0x82U, 0x00U, 0x04U,
             0x55U, 0xAAU, 0x5AU, 0xA5U, 0x83U, 0xFFU};
        if(length == sizeof(reset_command) && memcmp(frame, reset_command, length) == 0)
            (void)dwin_tx_submit_reset(frame, length, 2U);
    }
    else if(address == 0x1120U || address == 0x1500U ||
            address == 0x1600U || address == 0x1700U)
    {
        if(address == 0x1120U)
            (void)dwin_tx_submit_raw_latest(address, frame, length);
        else
            (void)dwin_tx_submit_raw_event(frame, length, 2U);
    }
}

/** @brief Process host address-discovery initialization data. */
static void app_process_init_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t device_id;
    uint16_t received[ADDR_COUNT];
    dwin_info_t discovered = {0};
    size_t received_count;

    if(length < 6U || frame[3] + 4U != length || frame[length - 1U] != 0xDCU || frame[3] < 2U)
        return;

    device_id = frame[4];
    received_count = (frame[3] - 2U) / 2U;
    if(received_count > ADDR_COUNT)
        return;

    if(device_id >= 1U && device_id <= 247U && device_id != param_rs485_addr_get())
    {
        PARAM_T param = get_param();

        param.Addr485 = device_id;
        (void)ParamQueueSend(param);
    }

    memset(received, 0, sizeof(received));
    acquire_mutex();
    for(uint8_t disk = 0U; disk < 6U; disk++)
        SetDiskInitStatusBit(disk, 0U);
    release_mutex();

    for(size_t i = 0U; i < received_count; i++)
    {
        uint16_t address = (uint16_t)(((uint16_t)frame[5U + i * 2U] << 8) |
                                      frame[6U + i * 2U]);
        const address_mapping_t *mapping = app_find_mapping(address);
        received[i] = address;
        if(mapping && discovered.confirm_count < ADDR_COUNT)
        {
            discovered.confirm_addrs[discovered.confirm_count++] = address;
            if(address >= 0x1320U && address <= 0x1370U && ((address - 0x1320U) % 0x10U) == 0U)
            {
                acquire_mutex();
                SetDiskInitStatusBit((uint8_t)((address - 0x1320U) / 0x10U), 1U);
                release_mutex();
            }
            (void)app_send_display(address, mapping->normal_data, mapping->normal_length);
        }
    }

    for(size_t i = 0U; i < ADDR_COUNT; i++)
    {
        bool found = false;
        for(size_t j = 0U; j < received_count; j++)
            found = found || app_address_map[i].initial_addr == received[j];
        if(!found && discovered.missing_count < ADDR_COUNT)
            discovered.missing_addrs[discovered.missing_count++] =
                app_address_map[i].initial_addr;
    }

    discovered.init_state = 1U;
    acquire_mutex();
    server.dwin_info = discovered;
    release_mutex();
    app_server_refresh();
}

/** @brief Process one private host alarm event and latch the buzzer. */
static void app_process_alarm_frame(const uint8_t *frame, uint16_t length)
{
    uint16_t address;
    uint8_t event_type;
    const address_mapping_t *mapping;
    bool confirmed = false;

    if(length != 8U || frame[7] != 0xDCU)
        return;
    address = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
    event_type = frame[6];
    if(event_type != 0x03U)
        return;

    if(address == 0x13A0U)
    {
        bsp_system_reset();
        return;
    }
    if(address == 0x1380U)
    {
        acquire_mutex();
        SetWarningBit(5U, 1U);
        release_mutex();
    }
    else if(address == 0x1390U)
    {
        acquire_mutex();
        SetWarningBit(4U, 1U);
        release_mutex();
    }
    else
    {
        mapping = app_find_mapping(address);
        if(!mapping)
            return;
        acquire_mutex();
        for(size_t i = 0U; i < server.dwin_info.confirm_count; i++)
            confirmed = confirmed || server.dwin_info.confirm_addrs[i] == address;
        release_mutex();
        if(!confirmed)
            return;
        (void)app_send_display(address, mapping->abnormal_data, mapping->abnormal_length);
        (void)send_alarm_color_packet_with_offset(address, 0xF800U, event_type);
        acquire_mutex();
        SetDiskErrorStatusBit(address);
        release_mutex();
    }
    app_set_alarm();
}

/** @brief Dispatch one complete AB CD private-protocol frame. */
static void app_process_abcd_frame(const uint8_t *frame, uint16_t length)
{
    static const uint8_t heartbeat[] = {0xABU, 0xCDU, 0xEBU, 0x02U, 0xCCU, 0xDCU};

    switch(frame[2])
    {
        case 0xEAU:
            app_process_alarm_frame(frame, length);
            break;
        case 0xEBU:
            if(length == sizeof(heartbeat) && memcmp(frame, heartbeat, sizeof(heartbeat)) == 0)
                app_server_refresh();
            break;
        case 0xAEU:
            app_process_init_frame(frame, length);
            break;
        case 0xEEU:
            if(length == 6U && frame[4] == 0xAEU)
            {
                uint8_t response[6] =
                    {0xABU, 0xCDU, 0xEEU, 0x02U, get_init_state(), 0xDCU};
                (void)ux_device_cdc_acm_send(response, sizeof(response), 100U);
            }
            break;
        case 0xADU:
            if(length == 7U)
            {
                uint8_t response[11] = {0xABU, 0xCDU, 0xADU, 0x07U};
                VAR_T live;

                acquire_mutex();
                live = g_tVar;
                release_mutex();
                response[4] = (uint8_t)(live.AHT20TEMP >> 8);
                response[5] = (uint8_t)live.AHT20TEMP;
                response[6] = (uint8_t)(live.DS18B20 >> 8);
                response[7] = (uint8_t)live.DS18B20;
                response[8] = (uint8_t)(live.AHT20HUMI >> 8);
                response[9] = (uint8_t)live.AHT20HUMI;
                response[10] = 0xDCU;
                (void)ux_device_cdc_acm_send(response, sizeof(response), 100U);
            }
            break;
        default:
            break;
    }
}

/** @brief Feed a byte stream into the bounded dual-protocol parser. */
static void app_parser_feed(app_parser_t *parser, const uint8_t *data, uint16_t length)
{
    uint16_t offset = 0U;

    if(!parser || !data || length == 0U)
        return;
    if(length > sizeof(parser->data) - parser->length)
    {
        parser->length = 0U;
        if(length > sizeof(parser->data))
            return;
    }
    memcpy(parser->data + parser->length, data, length);
    parser->length = (uint16_t)(parser->length + length);

    while(parser->length - offset >= 2U)
    {
        uint16_t frame_length = 0U;
        uint8_t *frame = parser->data + offset;
        uint16_t available = (uint16_t)(parser->length - offset);

        if(frame[0] == 0xABU && frame[1] == 0xCDU)
        {
            if(available < 3U)
                break;
            switch(frame[2])
            {
                case 0xEAU: frame_length = 8U; break;
                case 0xEBU: frame_length = 6U; break;
                case 0xEEU: frame_length = 6U; break;
                case 0xADU: frame_length = 7U; break;
                case 0xAEU:
                    if(available < 4U)
                        goto parser_done;
                    frame_length = (uint16_t)(4U + frame[3]);
                    break;
                default:
                    offset++;
                    continue;
            }
            if(frame_length > sizeof(parser->data))
            {
                offset++;
                continue;
            }
            if(available < frame_length)
                break;
            app_process_abcd_frame(frame, frame_length);
            offset = (uint16_t)(offset + frame_length);
        }
        else if(frame[0] == 0x5AU && frame[1] == 0xA5U)
        {
            uint16_t calculated_crc;
            uint16_t received_crc;

            if(available < 3U)
                break;
            frame_length = (uint16_t)(3U + frame[2]);
            if(frame_length < 8U || frame_length > sizeof(parser->data))
            {
                offset++;
                continue;
            }
            if(available < frame_length)
                break;
            calculated_crc = dwin_protocol_crc16(
                frame + 3U,
                (size_t)(frame_length - 5U));
            received_crc = (uint16_t)(
                frame[frame_length - 2U] |
                ((uint16_t)frame[frame_length - 1U] << 8));
            if(!app_crc_enabled || calculated_crc == received_crc)
            {
                app_process_frame(frame, frame_length);
                app_server_refresh();
            }
            offset = (uint16_t)(offset + frame_length);
        }
        else
        {
            offset++;
        }
    }

parser_done:
    if(offset)
    {
        memmove(parser->data, parser->data + offset, parser->length - offset);
        parser->length = (uint16_t)(parser->length - offset);
    }
}

/** @brief Feed DWIN-side bytes into the shared protocol parser. */
void msg_analysis(unsigned char *message, int length)
{
    if(length > 0)
        app_parser_feed(&app_dwin_parser, message, (uint16_t)length);
}

/** @brief Compatibility parser entry for CRC-enabled DWIN-side data. */
void msg_analysis_crc(unsigned char *message, int length)
{
    msg_analysis(message, length);
}

/** @brief Feed USB CDC bytes into the host protocol parser. */
void usb_parse_crc(unsigned char *message, int length)
{
    if(length > 0)
        app_parser_feed(&app_usb_parser, message, (uint16_t)length);
}

/** @brief Initialize host protocol and heartbeat state. */
void app_server_init(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memset(&server, 0, sizeof(server));
    server.ping.tick = 5000U;
    if(primask == 0U)
        __enable_irq();
}

/** @brief Advance the host heartbeat timeout by one millisecond. */
void app_server_tick(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if(server.ping.tick)
        server.ping.tick--;
    if(server.ping.tick == 0U)
    {
        server.ping.tick = 6000U;
        server.ping.call = 1U;
    }
    if(primask == 0U)
        __enable_irq();
}

/** @brief Mark one valid host frame as heartbeat activity. */
void app_server_refresh(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    server.ping.state = 1U;
    if(primask == 0U)
        __enable_irq();
}

/** @brief Apply a pending host connectivity transition. */
void check_server_ping(void)
{
    uint32_t primask;
    uint8_t new_state;
    bool changed = false;

    primask = __get_PRIMASK();
    __disable_irq();
    if(!server.ping.call)
    {
        if(primask == 0U)
            __enable_irq();
        return;
    }
    server.ping.call = 0U;
    new_state = server.ping.state ? 1U : 0U;
    server.ping.state = 0U;
    if(new_state != server.connect_state)
    {
        server.connect_state = new_state;
        changed = true;
    }
    if(primask == 0U)
        __enable_irq();

    if(changed)
    {
        acquire_mutex();
        SetWarningBit(3U, new_state ? 0U : 1U);
        release_mutex();
        connect_msg_dispaly(new_state);
    }
}

/** @brief Return the current host connectivity state. */
uint8_t get_connect_state(void)
{
    uint32_t primask = __get_PRIMASK();
    uint8_t state;

    __disable_irq();
    state = server.connect_state;
    if(primask == 0U)
        __enable_irq();
    return state;
}

/** @brief Set the current host connectivity state. */
void set_connect_state(uint8_t state)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    server.connect_state = state ? 1U : 0U;
    if(primask == 0U)
        __enable_irq();
}

/** @brief Return the host initialization state. */
uint8_t get_init_state(void)
{
    uint8_t state;

    acquire_mutex();
    state = server.dwin_info.init_state;
    release_mutex();
    return state;
}

/** @brief Set the host initialization state. */
void set_init_state(uint8_t state)
{
    acquire_mutex();
    server.dwin_info.init_state = state ? 1U : 0U;
    release_mutex();
}

/** @brief Return whether private-frame CRC validation is enabled. */
bool is_crc_enabled(void)
{
    bool enabled;

    acquire_mutex();
    enabled = app_crc_enabled;
    release_mutex();
    return enabled;
}

/** @brief Configure private-frame CRC validation. */
void set_crc_enabled(bool enabled)
{
    acquire_mutex();
    app_crc_enabled = enabled;
    release_mutex();
}

/** @brief Preserve the legacy initialization hook; startup owns creation. */
void init_mutex(void)
{
}

/** @brief Lock shared host protocol state. */
void acquire_mutex(void)
{
    (void)tx_mutex_get(&app_state_mutex, TX_WAIT_FOREVER);
}

/** @brief Unlock shared host protocol state. */
void release_mutex(void)
{
    (void)tx_mutex_put(&app_state_mutex);
}

/** @brief Submit the current connectivity indicator as a latest value. */
void connect_msg_dispaly(uint8_t state)
{
    uint8_t payload[2] = {state ? 0x10U : 0xFFU, 0x00U};

    (void)dwin_tx_submit_write_latest(0x9000U,
                                      payload,
                                      sizeof(payload));
}

/** @brief Cache environmental readings for private-protocol replies. */
void update_environmental_params(float temperature, float humidity)
{
    acquire_mutex();
    server.environmental_param.temp = temperature;
    server.environmental_param.humi = humidity;
    release_mutex();
}

/** @brief Submit one ordered temperature alarm color transition. */
static void app_send_temperature_alarm(uint16_t address, bool high)
{
    uint16_t color = high ? 0xF800U : 0xFFFFU;
    uint8_t payload[2] = {(uint8_t)(color >> 8), (uint8_t)color};

    (void)dwin_tx_submit_write_event(address,
                                     payload,
                                     sizeof(payload),
                                     2U);
}

/** @brief Update temperature warnings and automatic fan override state. */
static void app_update_temperature_state(void)
{
    VAR_T live;
    int16_t temperatures[3];
    const int16_t limits[3] = {CPU_TEMP_LIMIT, GPU_TEMP_LIMIT, DS18B20_TEMP_LIMIT};
    const uint16_t addresses[3] = {0x8873U, 0x88B3U, 0x88C3U};
    bool any_high = false;
    ULONG now = tx_time_get();

    acquire_mutex();
    live = g_tVar;
    release_mutex();
    temperatures[0] = (int16_t)live.CpuTemp;
    temperatures[1] = (int16_t)live.GpuTemp;
    temperatures[2] = (int16_t)live.DS18B20;

    for(size_t i = 0U; i < 3U; i++)
    {
        bool high = temperatures[i] > limits[i];
        if(high != app_temperature_high[i])
        {
            app_temperature_high[i] = high;
            app_send_temperature_alarm(addresses[i], high);
            acquire_mutex();
            SetWarningBit((uint8_t)i, high ? 1U : 0U);
            release_mutex();
        }
        any_high = any_high || high;
    }

    if(param_fan_mode_get() != 0U)
        return;
    if(any_high)
    {
        app_fan_set_duty_by_auto(10000U);
        app_fan_restore_tick = 0U;
    }
    else if(app_fan_restore_tick == 0U)
    {
        app_fan_restore_tick = now + 5000U;
    }
    else if((LONG)(now - app_fan_restore_tick) >= 0)
    {
        app_fan_set_duty_by_auto(5000U);
        app_fan_restore_tick = 0U;
    }
}

/** @brief Public compatibility entry for temperature alarm processing. */
void handle_temperature_conditions(void)
{
    app_update_temperature_state();
}

/** @brief Queue the default white color for all resettable indicators. */
void ResetDiskColorAndEvent(void)
{
    static const uint16_t addresses[] =
        {0x8803U, 0x8813U, 0x8823U, 0x8833U, 0x8843U, 0x8853U,
         0x1500U, 0x1600U, 0x1700U};

    for(size_t i = 0U; i < sizeof(addresses) / sizeof(addresses[0]); i++)
        (void)send_alarm_color_packet(addresses[i], 0xFFFFU);
}

/** @brief Stop safely after an unrecoverable initialization failure. */
void Error_Handler(void)
{
    bsp_stop_on_error();
}
