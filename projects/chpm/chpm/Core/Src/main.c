#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_config.h"
#include "bsp_uart.h"
#include "bsp_health.h"
#include "board.h"
#include "bsp_system.h"
#include "app_services.h"

#include "tx_api.h"
#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"
#include "ux_device_descriptors.h"
#include "ux_dcd_stm32.h"

#include "ld_modbus_server.h"

#include "app_fan.h"
#include "app_main.h"
#include "app_task.h"
#include "drv_aht20.h"
#include "drv_ds18b20.h"
#include "bsp_i2c_soft.h"
#include "bsp_spi.h"
#include "drv_w25qxx.h"
#include "bsp_soft_timer.h"
#include "debug_log.h"
#include "data_utils.h"
#include "drv_dwin.h"
#include "drv_modbus_port.h"
#include "gpio.h"
#include "param.h"
#include "dwin_ldc_channel.h"
#include "tim.h"
#include "usb_otg.h"

#define APP_START_PRIORITY       2U
#define APP_COMM_PRIORITY        3U
#define APP_OUTPUT_PRIORITY      4U
#define APP_MONITOR_PRIORITY     5U
#define APP_SENSOR_PRIORITY      6U
#define APP_USB_RX_PRIORITY      7U
#define APP_MODBUS_PRIORITY      3U
#define APP_DWIN_RX_PRIORITY     3U

#define APP_THREAD_STACK_SIZE    2048U
#define APP_OUTPUT_SLOTS         12U
#define APP_OUTPUT_DATA_SIZE     260U
#define APP_USBX_POOL_SIZE       10240U
#define APP_USBX_MEMORY_SIZE     4096U
#define APP_PARSER_SIZE          512U

#define APP_MODBUS_BAUD_RATE     115200
#define APP_MODBUS_REGISTER_COUNT 25U
#define APP_MODBUS_COMPAT_UNIT   0xF4U
#ifndef APP_LEGACY_RELAY_PB1_ENABLE
#define APP_LEGACY_RELAY_PB1_ENABLE 0U
#endif

#define APP_EVENT_ALARM          (1UL << 0)
#define APP_EVENT_ALARM_STOP     (1UL << 1)

#define CPU_TEMP_LIMIT           8000U
#define GPU_TEMP_LIMIT           8000U
#define DS18B20_TEMP_LIMIT       3500U
#define AHT20_TEMP_LIMIT         4200
#define AHT20_HUMIDITY_LIMIT     9000U
#define DS18B20_CONVERSION_TICKS 750U

typedef struct
{
    uint16_t length;
    uint8_t used;
    uint8_t data[APP_OUTPUT_DATA_SIZE];
} app_output_slot_t;

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

static TX_THREAD app_start_thread;
static TX_THREAD app_comm_thread;
static TX_THREAD app_output_thread;
static TX_THREAD app_monitor_thread;
static TX_THREAD app_sensor_thread;
static TX_THREAD app_usb_rx_thread;
static TX_THREAD app_modbus_server_thread;
static TX_THREAD app_dwin_rx_thread;

static uint64_t app_start_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_comm_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_output_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_monitor_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_sensor_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_usb_rx_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_modbus_server_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];
static uint64_t app_dwin_rx_stack[APP_THREAD_STACK_SIZE / sizeof(uint64_t)];

static TX_QUEUE app_output_queue;
static ULONG app_output_queue_storage[APP_OUTPUT_SLOTS];
static TX_MUTEX app_output_mutex;
static TX_MUTEX app_state_mutex;
static TX_MUTEX app_param_mutex;
static TX_EVENT_FLAGS_GROUP app_events;
static app_output_slot_t app_output_slots[APP_OUTPUT_SLOTS];

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
static bool app_alarm_active;
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

static void app_start_entry(ULONG input);
static void app_comm_entry(ULONG input);
static void app_output_entry(ULONG input);
static void app_monitor_entry(ULONG input);
static void app_sensor_entry(ULONG input);
static void app_usb_rx_entry(ULONG input);
static void app_modbus_server_entry(ULONG input);
static void app_dwin_rx_entry(ULONG input);
static void app_create_threads(void);
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
static void app_modbus_apply_writes(const uint16_t *before);
static uint8_t app_modbus_write_exception(const uint8_t *query, int length);
static bool app_modbus_value_is_valid(uint16_t address, uint16_t value);
static int app_modbus_send(void *user, const uint8_t *data, size_t length);
static void app_modbus_send_exception(const uint8_t *query, int length,
                                      uint8_t exception);

int ux_device_cdc_acm_send(uint8_t *data, uint32_t length, uint32_t timeout);

void tx_application_define(VOID *first_unused_memory)
{
    UINT status;

    (void)first_unused_memory;
    (void)tx_mutex_create(&app_output_mutex, "output lock", TX_INHERIT);
    (void)tx_mutex_create(&app_state_mutex, "state lock", TX_INHERIT);
    (void)tx_mutex_create(&app_param_mutex, "parameter lock", TX_INHERIT);
    (void)tx_event_flags_create(&app_events, "app events");
    (void)tx_queue_create(&app_output_queue,
                          "display output",
                          TX_1_ULONG,
                          app_output_queue_storage,
                          sizeof(app_output_queue_storage));

    status = tx_byte_pool_create(&app_usbx_pool,
                                 "USBX pool",
                                 app_usbx_pool_buffer,
                                 sizeof(app_usbx_pool_buffer));
    if(status == TX_SUCCESS)
        (void)app_usb_device_init(&app_usbx_pool);

    (void)tx_thread_create(&app_start_thread,
                           "app start",
                           app_start_entry,
                           0U,
                           app_start_stack,
                           sizeof(app_start_stack),
                           APP_START_PRIORITY,
                           APP_START_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
}

int main(void)
{
    bsp_status_t status;

    System_Init();

    status = board_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        Error_Handler();
    if(bsp_health_init(0U) != BSP_STATUS_OK)
        Error_Handler();
    MX_USB_OTG_FS_PCD_Init();

    HAL_SuspendTick();
    tx_kernel_enter();

    while(1)
    {
    }
}

static void app_start_entry(ULONG input)
{
    const app_services_config_t services_config =
    {
        .modbus_baud_rate = APP_MODBUS_BAUD_RATE,
        .tick_1ms_hook = app_server_tick
    };

    (void)input;
    HAL_ResumeTick();
    bsp_Init();
    bsp_InitSPIBus();
    bsp_InitSFlash();
    ParamLoad();

    app_server_init();
    if(app_services_init(&services_config) != BSP_STATUS_OK)
        Error_Handler();

    app_fan_init();
    bsp_InitI2C();
    (void)AHT20_Reset();
    tx_thread_sleep(20U);
    (void)AHT20_Init();
    (void)DS18B20_Init();
    ResetDiskColorAndEvent();

    app_create_threads();

    for(;;)
    {
        bsp_health_poll();
        tx_thread_sleep(1000U);
    }
}

static void app_create_threads(void)
{
    (void)tx_thread_create(&app_comm_thread, "comm", app_comm_entry, 0U,
                           app_comm_stack, sizeof(app_comm_stack),
                           APP_COMM_PRIORITY, APP_COMM_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
    (void)tx_thread_create(&app_output_thread, "display output", app_output_entry, 0U,
                           app_output_stack, sizeof(app_output_stack),
                           APP_OUTPUT_PRIORITY, APP_OUTPUT_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
    (void)tx_thread_create(&app_monitor_thread, "monitor", app_monitor_entry, 0U,
                           app_monitor_stack, sizeof(app_monitor_stack),
                           APP_MONITOR_PRIORITY, APP_MONITOR_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
    (void)tx_thread_create(&app_sensor_thread, "sensors", app_sensor_entry, 0U,
                           app_sensor_stack, sizeof(app_sensor_stack),
                           APP_SENSOR_PRIORITY, APP_SENSOR_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
    (void)tx_thread_create(&app_usb_rx_thread, "USB RX", app_usb_rx_entry, 0U,
                           app_usb_rx_stack, sizeof(app_usb_rx_stack),
                           APP_USB_RX_PRIORITY, APP_USB_RX_PRIORITY,
                           TX_NO_TIME_SLICE, TX_AUTO_START);
    (void)tx_thread_create(&app_modbus_server_thread,
                           "ld_modbus server",
                           app_modbus_server_entry,
                           0U,
                           app_modbus_server_stack,
                           sizeof(app_modbus_server_stack),
                           APP_MODBUS_PRIORITY,
                           APP_MODBUS_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
    (void)tx_thread_create(&app_dwin_rx_thread,
                           "DWIN LDC owner",
                           app_dwin_rx_entry,
                           0U,
                           app_dwin_rx_stack,
                           sizeof(app_dwin_rx_stack),
                           APP_DWIN_RX_PRIORITY,
                           APP_DWIN_RX_PRIORITY,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
}

static void app_modbus_refresh_registers(void)
{
    app_modbus_registers[REG_MODBUS_ADDRESS] = param_rs485_addr_get();
    app_modbus_registers[REG_FAN_MODE] = param_fan_mode_get();
    app_modbus_registers[REG_FAN_SPEED] = param_fan_mode_get() ?
                                                   g_tVar.pwm_manual : g_tParam.pwm_auto;
    app_modbus_registers[REG_DISK_STATUS] = g_tVar.DiskInitStatus;
    app_modbus_registers[REG_WARNING_STATUS] = g_tVar.DevWarning;
    app_modbus_registers[REG_CPU_USAGE] = g_tVar.CpuUsage;
    app_modbus_registers[REG_CPU_TEMP] = g_tVar.CpuTemp;
    app_modbus_registers[REG_FAN_RATE] = g_tVar.FanRate;
    app_modbus_registers[REG_CPU_SPEED] = g_tVar.CpuSpeed;
    app_modbus_registers[REG_GPU_USAGE] = g_tVar.GpuUsage;
    app_modbus_registers[REG_GPU_TEMP] = g_tVar.GpuTemp;
    app_modbus_registers[REG_GPU_SPEED] = g_tVar.GpuSpeed;
    app_modbus_registers[REG_RAM_USAGE] = g_tVar.RamUsage;
    app_modbus_registers[REG_RAM_AVAILABLE] = g_tVar.RamAvMemory;
    app_modbus_registers[REG_MAINBOARD_VOLTAGE] = g_tVar.MbVbatV;
    for(uint8_t i = 0U; i < 6U; i++)
        app_modbus_registers[REG_DISK_USAGE_1 + i] =
            GetDiskInitStatusBit(i) ? g_tVar.DiskUsage[i] : 0U;
    app_modbus_registers[REG_DS18B20_TEMP] = g_tVar.DS18B20;
    app_modbus_registers[REG_AHT20_TEMP] = g_tVar.AHT20TEMP;
    app_modbus_registers[REG_AHT20_HUMIDITY] = g_tVar.AHT20HUMI;
    app_modbus_registers[REG_RESTART_COUNT] = g_tParam.RestartCnt;
}

static void app_modbus_apply_writes(const uint16_t *before)
{
    bool changed = false;
    uint16_t value;

    (void)tx_mutex_get(&app_param_mutex, TX_WAIT_FOREVER);
    value = app_modbus_registers[REG_MODBUS_ADDRESS];
    if(value != before[REG_MODBUS_ADDRESS] && value >= 1U && value <= 247U)
    {
        param_rs485_addr_set((uint8_t)value);
        changed = true;
    }

    value = app_modbus_registers[REG_FAN_MODE];
    if(value != before[REG_FAN_MODE] && value <= 1U)
    {
        param_fan_mode_set((uint8_t)value);
        g_tVar.mode = (uint8_t)value;
        if(value)
            app_fan_set_duty(g_tVar.pwm_manual);
        else
            app_fan_set_duty_by_auto(g_tVar.pwm_auto);
        changed = true;
    }

    value = app_modbus_registers[REG_FAN_SPEED];
    if(value != before[REG_FAN_SPEED] && param_fan_mode_get())
    {
        if(value < 4000U)
            value = 4000U;
        if(value > 10000U)
            value = 10000U;
        g_tVar.pwm_manual = value;
        param_pwm_manual_set(value);
        app_fan_set_duty(value);
        changed = true;
    }

    if(changed)
        Param_Store(g_tParam.Addr485,
                    g_tParam.mode,
                    g_tParam.RestartCnt,
                    g_tParam.pwm_manual,
                    g_tParam.pwm_auto);
    (void)tx_mutex_put(&app_param_mutex);
}

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

static int app_modbus_send(void *user, const uint8_t *data, size_t length)
{
    (void)user;
    if(length > UINT16_MAX)
        return -1;
    return drv_modbus_port_write(data, (uint16_t)length, 100U) == BSP_STATUS_OK ?
           (int)length : -1;
}

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
       (view.unit_id != param_rs485_addr_get() &&
        view.unit_id != APP_MODBUS_COMPAT_UNIT) || view.pdu_length == 0U)
        return;
    pdu[0] = (uint8_t)(view.pdu[0] | 0x80U);
    pdu[1] = exception;
    if(ld_modbus_rtu_encode(param_rs485_addr_get(), pdu, sizeof(pdu),
                            response, sizeof(response), &response_length) ==
       LD_MODBUS_STATUS_OK)
        (void)app_modbus_send(NULL, response, response_length);
}

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
        {
            app_modbus_apply_writes(before);
            if(action == LD_MODBUS_SERVER_ACTION_REPLY)
                (void)app_modbus_send(NULL, response, response_length);
        }
    }
}

static void app_dwin_rx_entry(ULONG input)
{
    (void)input;
    for(;;)
        dwin_ldc_channel_owner_wait(TX_WAIT_FOREVER);
}

static void app_comm_entry(ULONG input)
{
    (void)input;

    for(;;)
    {
        app_services_tick_1ms();
        tx_thread_sleep(1U);
    }
}

static void app_output_entry(ULONG input)
{
    ULONG message;
    app_output_slot_t *slot;
    bool reset_after_send;

    (void)input;
    for(;;)
    {
        if(tx_queue_receive(&app_output_queue, &message, TX_WAIT_FOREVER) != TX_SUCCESS)
            continue;

        slot = (app_output_slot_t *)message;
        reset_after_send = slot->length > 0U && slot->data[0] == DEVICE_RST;

        if(slot->length > 1U && slot->data[0] >= DEVICE_AHT20 && slot->data[0] <= TEMP_COLOR)
            (void)drv_dwin_write(slot->data + 1U,
                                 slot->length - 1U,
                                 0xffU);
        else if(slot->length > 0U)
            (void)drv_dwin_write(slot->data, slot->length, 0xffU);

        (void)tx_mutex_get(&app_output_mutex, TX_WAIT_FOREVER);
        slot->used = 0U;
        slot->length = 0U;
        (void)tx_mutex_put(&app_output_mutex);

        if(reset_after_send)
        {
            tx_thread_sleep(20U);
            NVIC_SystemReset();
        }
    }
}

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

        if((LONG)(now - next_aht) >= 0)
        {
            float temperature;
            float humidity;

            if(AHT20_StartMeasurement())
            {
                tx_thread_sleep(100U);
                if(AHT20_ReadMeasurement(&temperature, &humidity))
                {
                    uint8_t payload[8];
                    uint8_t packet[17];
                    uint8_t packet_length;
                    int16_t temperature_scaled = (int16_t)(temperature * 100.0f);
                    uint16_t humidity_scaled = (uint16_t)(humidity * 100.0f);
                    uint32_t raw;

                    SetGlobalVar(AHT20TEMP, &temperature_scaled);
                    SetGlobalVar(AHT20HUMI, &humidity_scaled);
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
                    packet_length = BuildPacketWithID(0x1162U,
                                                      DEVICE_AHT20,
                                                      payload,
                                                      sizeof(payload),
                                                      packet);
                    (void)enqueue_data(packet, packet_length);

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
            if(DS18B20_StartConversion())
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

            if(DS18B20_ReadTemperature(&temperature))
            {
                uint8_t payload[4];
                uint8_t packet[16];
                int16_t scaled = (int16_t)(temperature * 100.0f);
                uint8_t packet_length;
                uint32_t raw;

                ds18b20_temp = temperature;
                SetGlobalVar(DS18B20, &scaled);
                memcpy(&raw, &temperature, sizeof(raw));
                payload[0] = (uint8_t)(raw >> 24);
                payload[1] = (uint8_t)(raw >> 16);
                payload[2] = (uint8_t)(raw >> 8);
                payload[3] = (uint8_t)raw;
                packet_length = BuildPacketWithID(0x1160U,
                                                  DEVICE_DS18B20,
                                                  payload,
                                                  sizeof(payload),
                                                  packet);
                (void)enqueue_data(packet, packet_length);
            }
            ds_pending = false;
            next_ds = now + 10000U;
        }

        tx_thread_sleep(50U);
    }
}

static void app_monitor_entry(ULONG input)
{
    ULONG next_slow = 0U;
    ULONG next_led = 0U;
    ULONG next_alarm = 0U;

    (void)input;
    for(;;)
    {
        ULONG now = tx_time_get();
        ULONG actual_events = 0U;

        app_fan_process();
        (void)tx_event_flags_get(&app_events,
                                 APP_EVENT_ALARM | APP_EVENT_ALARM_STOP,
                                 TX_OR_CLEAR,
                                 &actual_events,
                                 TX_NO_WAIT);
        if(actual_events & APP_EVENT_ALARM)
            app_alarm_active = true;
        if(actual_events & APP_EVENT_ALARM_STOP)
            app_alarm_active = false;

        if(app_alarm_active && (LONG)(now - next_alarm) >= 0)
        {
            uint8_t payload[2] = {0x00U, 0x3EU};
            uint8_t packet[16];
            uint8_t length = BuildPacketWithID(0x00A0U,
                                               DEVICE_ALARM,
                                               payload,
                                               sizeof(payload),
                                               packet);
            (void)enqueue_data(packet, length);
            next_alarm = now + 5000U;
        }

        if((LONG)(now - next_slow) >= 0)
        {
            check_server_ping();
            app_update_temperature_state();
            next_slow = now + 1000U;
        }

        if((LONG)(now - next_led) >= 0)
        {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            next_led = now + (get_connect_state() ? 1000U : 200U);
        }

        tx_thread_sleep(20U);
    }
}

static void app_usb_rx_entry(ULONG input)
{
    UCHAR data[64];
    ULONG actual_length;

    (void)input;
    for(;;)
    {
        UX_SLAVE_CLASS_CDC_ACM *instance = app_cdc_instance;
        if(!instance)
        {
            tx_thread_sleep(20U);
            continue;
        }

        actual_length = 0U;
        if(ux_device_class_cdc_acm_read(instance,
                                        data,
                                        sizeof(data),
                                        &actual_length) == UX_SUCCESS &&
           actual_length > 0U)
        {
            usb_parse_crc(data, (int)actual_length);
        }
    }
}

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

    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 128U);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0U, 64U);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1U, 128U);

    if(ux_dcd_stm32_initialize((ULONG)USB_OTG_FS,
                               (ULONG)&hpcd_USB_OTG_FS) != UX_SUCCESS)
        return UX_ERROR;

    if(HAL_PCD_Start(&hpcd_USB_OTG_FS) != HAL_OK)
        return UX_ERROR;
    return UX_SUCCESS;
}

static UINT app_usb_state_change(ULONG state)
{
    (void)state;
    return UX_SUCCESS;
}

static VOID app_usb_activate(VOID *instance)
{
    app_cdc_instance = (UX_SLAVE_CLASS_CDC_ACM *)instance;
}

static VOID app_usb_deactivate(VOID *instance)
{
    if(app_cdc_instance == instance)
        app_cdc_instance = UX_NULL;
}

static VOID app_usb_parameter_change(VOID *instance)
{
    (void)instance;
}

int ux_device_cdc_acm_send(uint8_t *data, uint32_t length, uint32_t timeout)
{
    ULONG actual_length = 0U;
    UX_SLAVE_CLASS_CDC_ACM *instance = app_cdc_instance;

    (void)timeout;
    if(!instance || !data || length == 0U)
        return -1;
    return ux_device_class_cdc_acm_write(instance,
                                         data,
                                         length,
                                         &actual_length) == UX_SUCCESS &&
           actual_length == length ? 0 : -1;
}

int ux_device_cdc_acm_getchar(uint8_t *data, uint32_t timeout)
{
    ULONG actual_length = 0U;
    UX_SLAVE_CLASS_CDC_ACM *instance = app_cdc_instance;

    (void)timeout;
    if(!instance || !data)
        return -1;
    return ux_device_class_cdc_acm_read(instance, data, 1U, &actual_length) == UX_SUCCESS &&
           actual_length == 1U ? 0 : -1;
}

int ux_device_cdc_acm_flush(void)
{
    return 0;
}

bool enqueue_data(const uint8_t *data, size_t length)
{
    app_output_slot_t *slot = NULL;
    ULONG message;

    if(!data || length == 0U || length > APP_OUTPUT_DATA_SIZE)
        return false;
    if(tx_mutex_get(&app_output_mutex, TX_NO_WAIT) != TX_SUCCESS)
        return false;

    for(size_t i = 0U; i < APP_OUTPUT_SLOTS; i++)
    {
        if(!app_output_slots[i].used)
        {
            slot = &app_output_slots[i];
            slot->used = 1U;
            slot->length = (uint16_t)length;
            memcpy(slot->data, data, length);
            break;
        }
    }
    (void)tx_mutex_put(&app_output_mutex);

    if(!slot)
        return false;

    message = (ULONG)slot;
    if(tx_queue_send(&app_output_queue, &message, TX_NO_WAIT) != TX_SUCCESS)
    {
        (void)tx_mutex_get(&app_output_mutex, TX_WAIT_FOREVER);
        slot->used = 0U;
        slot->length = 0U;
        (void)tx_mutex_put(&app_output_mutex);
        return false;
    }
    return true;
}

void ParamQueueSend(PARAM_T param)
{
    if(param.Addr485 < 1U || param.Addr485 > 247U || param.mode > 1U ||
       param.pwm_manual < 4000U || param.pwm_manual > 10000U ||
       param.pwm_auto < 4000U || param.pwm_auto > 10000U)
        return;

    (void)tx_mutex_get(&app_param_mutex, TX_WAIT_FOREVER);
    g_tParam = param;
    Param_Store(param.Addr485,
                param.mode,
                param.RestartCnt,
                param.pwm_manual,
                param.pwm_auto);
    (void)tx_mutex_put(&app_param_mutex);
}

void app_set_alarm(void)
{
    (void)tx_event_flags_set(&app_events, APP_EVENT_ALARM, TX_OR);
}

void app_set_alarm_stop(void)
{
    (void)tx_event_flags_set(&app_events, APP_EVENT_ALARM_STOP, TX_OR);
}

void set_event_bits(uint32_t bits)
{
    (void)tx_event_flags_set(&app_events, (ULONG)bits, TX_OR);
}

uint8_t BuildPacketWithID(uint16_t address,
                          uint8_t device_id,
                          const uint8_t *data,
                          size_t length,
                          uint8_t *output)
{
    size_t position = 0U;
    uint16_t crc;

    if(!output || (!data && length) || length > 246U)
        return 0U;

    output[position++] = device_id;
    output[position++] = 0x5AU;
    output[position++] = 0xA5U;
    output[position++] = (uint8_t)(length + 5U);
    output[position++] = 0x82U;
    output[position++] = (uint8_t)(address >> 8);
    output[position++] = (uint8_t)address;
    if(length)
    {
        memcpy(output + position, data, length);
        position += length;
    }
    crc = CRC16_Modbus(output + 4U, (uint16_t)(position - 4U));
    output[position++] = (uint8_t)(crc >> 8);
    output[position++] = (uint8_t)crc;
    return (uint8_t)position;
}

uint8_t build_packet_with_crc1(uint16_t address,
                               uint8_t device_id,
                               const uint8_t *data,
                               size_t length,
                               uint8_t *output)
{
    return BuildPacketWithID(address, device_id, data, length, output);
}

void build_packet_with_crc(uint16_t address,
                           const uint8_t *data,
                           size_t length,
                           uint8_t *output,
                           bool enable_crc)
{
    size_t position = 0U;
    uint16_t crc;

    if(!output || (!data && length) || length > 247U)
        return;
    output[position++] = 0x5AU;
    output[position++] = 0xA5U;
    output[position++] = (uint8_t)(length + (enable_crc ? 5U : 3U));
    output[position++] = 0x82U;
    output[position++] = (uint8_t)(address >> 8);
    output[position++] = (uint8_t)address;
    if(length)
    {
        memcpy(output + position, data, length);
        position += length;
    }
    if(enable_crc)
    {
        crc = CRC16_Modbus(output + 3U, (uint16_t)(position - 3U));
        output[position++] = (uint8_t)(crc >> 8);
        output[position++] = (uint8_t)crc;
    }
}

bool send_packet_with_crc(uint16_t address, uint8_t *data, uint8_t length, bool enable_crc)
{
    uint8_t packet[APP_OUTPUT_DATA_SIZE];
    size_t packet_length = 6U + length + (enable_crc ? 2U : 0U);

    build_packet_with_crc(address, data, length, packet, enable_crc);
    return enqueue_data(packet, packet_length);
}

bool send_alarm_color_packet(uint16_t address, uint16_t color)
{
    uint8_t payload[2] = {(uint8_t)(color >> 8), (uint8_t)color};
    uint8_t packet[16];
    uint8_t length = BuildPacketWithID(address,
                                       TEMP_COLOR,
                                       payload,
                                       sizeof(payload),
                                       packet);
    return enqueue_data(packet, length);
}

bool send_page_switch_command(uint8_t page)
{
    uint8_t payload[4] = {0x5AU, 0x01U, 0x00U, page};
    uint8_t packet[16];
    uint8_t length = BuildPacketWithID(0x0084U,
                                       DEVICE_PAGE,
                                       payload,
                                       sizeof(payload),
                                       packet);
    return enqueue_data(packet, length);
}

bool send_page_switch_command_ex(uint8_t page)
{
    return send_page_switch_command(page);
}

static const address_mapping_t *app_find_mapping(uint16_t address)
{
    for(size_t i = 0U; i < ADDR_COUNT; i++)
    {
        if(app_address_map[i].initial_addr == address)
            return &app_address_map[i];
    }
    return NULL;
}

static bool app_send_display(uint16_t event_address, const uint8_t *data, size_t length)
{
    const address_mapping_t *mapping = app_find_mapping(event_address);
    uint8_t packet[32];
    uint8_t packet_length;

    if(!mapping || length > 20U)
        return false;
    packet_length = BuildPacketWithID(mapping->corresponding_addr,
                                      DEVICE_INIT,
                                      data,
                                      length,
                                      packet);
    return enqueue_data(packet, packet_length);
}

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

static void app_refresh_computer_data(const uint8_t *frame, uint16_t length)
{
    uint16_t value;
    uint32_t fan_rate;
    size_t disk_offset = 46U;
    static const uint8_t invalid_gpu[4] = {0xBFU, 0x80U, 0x00U, 0x00U};

    if(length < 46U)
        return;

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
}

static void app_process_frame(uint8_t *frame, uint16_t length)
{
    uint16_t address;
    uint8_t packet[APP_OUTPUT_DATA_SIZE];

    if(!frame || length < 6U || length + 1U > sizeof(packet))
        return;
    address = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);

    if(address == 0x1100U)
    {
        app_refresh_computer_data(frame, length);
        packet[0] = DEVICE_FORWARD;
        memcpy(packet + 1U, frame, length);
        (void)enqueue_data(packet, length + 1U);
    }
    else if(address == 0x0004U)
    {
        static const uint8_t reset_command[] =
            {0x5AU, 0xA5U, 0x09U, 0x82U, 0x00U, 0x04U,
             0x55U, 0xAAU, 0x5AU, 0xA5U, 0x83U, 0xFFU};
        if(length == sizeof(reset_command) && memcmp(frame, reset_command, length) == 0)
        {
            packet[0] = DEVICE_RST;
            memcpy(packet + 1U, frame, length);
            (void)enqueue_data(packet, length + 1U);
        }
    }
    else if(address == 0x1120U || address == 0x1500U ||
            address == 0x1600U || address == 0x1700U)
    {
        packet[0] = DEVICE_FORWARD;
        memcpy(packet + 1U, frame, length);
        (void)enqueue_data(packet, length + 1U);
    }
}

static void app_process_init_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t device_id;
    uint16_t received[ADDR_COUNT];
    size_t received_count;

    if(length < 6U || frame[3] + 4U != length || frame[length - 1U] != 0xDCU || frame[3] < 2U)
        return;

    device_id = frame[4];
    received_count = (frame[3] - 2U) / 2U;
    if(received_count > ADDR_COUNT)
        return;

    if(device_id >= 1U && device_id <= 247U && device_id != param_rs485_addr_get())
    {
        PARAM_T param;
        param_rs485_addr_set(device_id);
        param = get_param();
        param.Addr485 = device_id;
        ParamQueueSend(param);
    }

    memset(received, 0, sizeof(received));
    server.dwin_info.confirm_count = 0U;
    server.dwin_info.missing_count = 0U;
    for(uint8_t disk = 0U; disk < 6U; disk++)
        SetDiskInitStatusBit(disk, 0U);

    for(size_t i = 0U; i < received_count; i++)
    {
        uint16_t address = (uint16_t)(((uint16_t)frame[5U + i * 2U] << 8) |
                                      frame[6U + i * 2U]);
        const address_mapping_t *mapping = app_find_mapping(address);
        received[i] = address;
        if(mapping && server.dwin_info.confirm_count < ADDR_COUNT)
        {
            server.dwin_info.confirm_addrs[server.dwin_info.confirm_count++] = address;
            if(address >= 0x1320U && address <= 0x1370U && ((address - 0x1320U) % 0x10U) == 0U)
                SetDiskInitStatusBit((uint8_t)((address - 0x1320U) / 0x10U), 1U);
            (void)app_send_display(address, mapping->normal_data, mapping->normal_length);
        }
    }

    for(size_t i = 0U; i < ADDR_COUNT; i++)
    {
        bool found = false;
        for(size_t j = 0U; j < received_count; j++)
            found = found || app_address_map[i].initial_addr == received[j];
        if(!found && server.dwin_info.missing_count < ADDR_COUNT)
            server.dwin_info.missing_addrs[server.dwin_info.missing_count++] =
                app_address_map[i].initial_addr;
    }

    server.dwin_info.init_state = 1U;
    app_server_refresh();
}

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
        NVIC_SystemReset();
        return;
    }
    if(address == 0x1380U)
        SetWarningBit(5U, 1U);
    else if(address == 0x1390U)
        SetWarningBit(4U, 1U);
    else
    {
        mapping = app_find_mapping(address);
        if(!mapping)
            return;
        for(size_t i = 0U; i < server.dwin_info.confirm_count; i++)
            confirmed = confirmed || server.dwin_info.confirm_addrs[i] == address;
        if(!confirmed)
            return;
        (void)app_send_display(address, mapping->abnormal_data, mapping->abnormal_length);
        (void)send_alarm_color_packet_with_offset(address, 0xF800U, event_type);
        SetDiskErrorStatusBit(address);
    }
    app_set_alarm();
}

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
                    {0xABU, 0xCDU, 0xEEU, 0x02U, server.dwin_info.init_state, 0xDCU};
                (void)ux_device_cdc_acm_send(response, sizeof(response), 100U);
            }
            break;
        case 0xADU:
            if(length == 7U)
            {
                uint8_t response[11] = {0xABU, 0xCDU, 0xADU, 0x07U};
                response[4] = (uint8_t)(g_tVar.AHT20TEMP >> 8);
                response[5] = (uint8_t)g_tVar.AHT20TEMP;
                response[6] = (uint8_t)(g_tVar.DS18B20 >> 8);
                response[7] = (uint8_t)g_tVar.DS18B20;
                response[8] = (uint8_t)(g_tVar.AHT20HUMI >> 8);
                response[9] = (uint8_t)g_tVar.AHT20HUMI;
                response[10] = 0xDCU;
                (void)ux_device_cdc_acm_send(response, sizeof(response), 100U);
            }
            break;
        default:
            break;
    }
}

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
            calculated_crc = CRC16_Modbus(frame + 3U, (uint16_t)(frame_length - 5U));
            received_crc = (uint16_t)(((uint16_t)frame[frame_length - 2U] << 8) |
                                      frame[frame_length - 1U]);
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

void msg_analysis(unsigned char *message, int length)
{
    if(length > 0)
        app_parser_feed(&app_dwin_parser, message, (uint16_t)length);
}

void msg_analysis_crc(unsigned char *message, int length)
{
    msg_analysis(message, length);
}

void usb_parse_crc(unsigned char *message, int length)
{
    if(length > 0)
        app_parser_feed(&app_usb_parser, message, (uint16_t)length);
}

void app_server_init(void)
{
    memset(&server, 0, sizeof(server));
    server.ping.tick = 5000U;
}

void app_server_tick(void)
{
    if(server.ping.tick)
        server.ping.tick--;
    if(server.ping.tick == 0U)
    {
        server.ping.tick = 6000U;
        server.ping.call = 1U;
    }
}

void app_server_refresh(void)
{
    server.ping.state = 1U;
}

void check_server_ping(void)
{
    uint8_t new_state;

    if(!server.ping.call)
        return;
    server.ping.call = 0U;
    new_state = server.ping.state ? 1U : 0U;
    server.ping.state = 0U;
    if(new_state != server.connect_state)
    {
        server.connect_state = new_state;
        SetWarningBit(3U, new_state ? 0U : 1U);
        connect_msg_dispaly(new_state);
    }
}

uint8_t get_connect_state(void)
{
    return server.connect_state;
}

void set_connect_state(uint8_t state)
{
    server.connect_state = state ? 1U : 0U;
}

uint8_t get_init_state(void)
{
    return server.dwin_info.init_state;
}

void set_init_state(uint8_t state)
{
    server.dwin_info.init_state = state ? 1U : 0U;
}

bool is_crc_enabled(void)
{
    return app_crc_enabled;
}

void set_crc_enabled(bool enabled)
{
    app_crc_enabled = enabled;
}

void init_mutex(void)
{
}

void acquire_mutex(void)
{
    (void)tx_mutex_get(&app_state_mutex, TX_WAIT_FOREVER);
}

void release_mutex(void)
{
    (void)tx_mutex_put(&app_state_mutex);
}

void connect_msg_dispaly(uint8_t state)
{
    uint8_t payload[2] = {state ? 0x10U : 0xFFU, 0x00U};
    uint8_t packet[16];
    uint8_t length = BuildPacketWithID(0x9000U,
                                       DEVICE_CONNECT,
                                       payload,
                                       sizeof(payload),
                                       packet);
    (void)enqueue_data(packet, length);
}

void update_environmental_params(float temperature, float humidity)
{
    server.environmental_param.temp = temperature;
    server.environmental_param.humi = humidity;
}

static void app_send_temperature_alarm(uint16_t address, bool high)
{
    uint16_t color = high ? 0xF800U : 0xFFFFU;
    uint8_t payload[2] = {(uint8_t)(color >> 8), (uint8_t)color};
    uint8_t packet[16];
    uint8_t length = BuildPacketWithID(address,
                                       TEMP_ALARM,
                                       payload,
                                       sizeof(payload),
                                       packet);
    (void)enqueue_data(packet, length);
}

static void app_update_temperature_state(void)
{
    int16_t temperatures[3] = {(int16_t)get_temp(2U),
                               (int16_t)get_temp(3U),
                               (int16_t)get_temp(1U)};
    const int16_t limits[3] = {CPU_TEMP_LIMIT, GPU_TEMP_LIMIT, DS18B20_TEMP_LIMIT};
    const uint16_t addresses[3] = {0x8873U, 0x88B3U, 0x88C3U};
    bool any_high = false;
    ULONG now = tx_time_get();

    for(size_t i = 0U; i < 3U; i++)
    {
        bool high = temperatures[i] > limits[i];
        if(high != app_temperature_high[i])
        {
            app_temperature_high[i] = high;
            app_send_temperature_alarm(addresses[i], high);
            SetWarningBit((uint8_t)i, high ? 1U : 0U);
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

void handle_temperature_conditions(void)
{
    app_update_temperature_state();
}

void ResetDiskColorAndEvent(void)
{
    static const uint16_t addresses[] =
        {0x8803U, 0x8813U, 0x8823U, 0x8833U, 0x8843U, 0x8853U,
         0x1500U, 0x1600U, 0x1700U};

    for(size_t i = 0U; i < sizeof(addresses) / sizeof(addresses[0]); i++)
        (void)send_alarm_color_packet(addresses[i], 0xFFFFU);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM11)
        HAL_IncTick();
}

void Error_Handler(void)
{
    __disable_irq();
    for(;;)
    {
    }
}
