#include "app_modbus_rtu.h"

#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "ld_modbus_client.h"
#include "ld_modbus_ldc.h"
#include "tx_api.h"

#define APP_MODBUS_TASK_PRIORITY                4U
#define APP_MODBUS_TASK_STACK_SIZE           4096U
#define APP_MODBUS_FRAME_TIMEOUT_US          1750U
#define APP_MODBUS_PACKET_COUNT                 8U
#define APP_MODBUS_COIL_COUNT                   16U
#define APP_MODBUS_DISCRETE_COUNT               16U
#define APP_MODBUS_HOLDING_COUNT                16U
#define APP_MODBUS_INPUT_COUNT                  16U
#define APP_MODBUS_FAILURES_BEFORE_OFFLINE       3U
#define APP_MODBUS_MAX_COMMAND_BURST             4U
#define APP_MODBUS_EVENT_CONFIG_PENDING       0x01UL

#if BSP_UART5_BAUD_RATE != APP_MODBUS_RTU_BAUD_RATE
#error "UART5 baud rate must match the Modbus RTU application setting"
#endif

typedef enum
{
    APP_MODBUS_TRANSACTION_NONE = 0,
    APP_MODBUS_TRANSACTION_POLL,
    APP_MODBUS_TRANSACTION_COMMAND
} app_modbus_transaction_kind_t;

typedef enum
{
    APP_MODBUS_TRANSACTION_IDLE = 0,
    APP_MODBUS_TRANSACTION_TX_DRAIN,
    APP_MODBUS_TRANSACTION_WAIT_RESPONSE
} app_modbus_transaction_stage_t;

typedef struct
{
    app_modbus_master_device_status_t status;
    ULONG next_due[APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT];
} app_modbus_master_device_runtime_t;

typedef struct
{
    app_modbus_command_t command;
    uint32_t id;
} app_modbus_command_entry_t;

typedef struct
{
    uint8_t kind;
    uint8_t stage;
    uint8_t device_index;
    uint8_t register_class;
    uint8_t function;
    uint8_t reserved;
    uint16_t address;
    uint16_t quantity;
    uint16_t value;
    uint32_t command_id;
    ULONG deadline;
} app_modbus_transaction_t;

static const uint32_t modbus_backoff_ms[] =
{
    1000UL,
    2000UL,
    5000UL,
    10000UL,
    30000UL,
    60000UL
};

static TX_THREAD modbus_task_control_block;
static TX_EVENT_FLAGS_GROUP modbus_control_events;
static TX_MUTEX modbus_state_mutex;
static uint64_t modbus_task_stack[APP_MODBUS_TASK_STACK_SIZE / sizeof(uint64_t)];

static ldc_easy_t modbus_queue;
static uint8_t modbus_ring[LDC_EASY_RING_BYTES(LD_MODBUS_RTU_MAX_ADU_LENGTH,
                                                APP_MODBUS_PACKET_COUNT)];
static ldc_packet_t modbus_packets[APP_MODBUS_PACKET_COUNT];
static uint8_t modbus_request[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t modbus_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t modbus_pdu[LD_MODBUS_MAX_PDU_LENGTH];
static uint8_t modbus_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static ld_modbus_ldc_rtu_server_t modbus_server;
static ld_modbus_server_map_t modbus_map;

static uint8_t modbus_coils[APP_MODBUS_COIL_COUNT];
static uint8_t modbus_discrete_inputs[APP_MODBUS_DISCRETE_COUNT];
static uint16_t modbus_holding_registers[APP_MODBUS_HOLDING_COUNT];
static uint16_t modbus_input_registers[APP_MODBUS_INPUT_COUNT];

static app_modbus_rtu_config_t modbus_active_config;
static app_modbus_rtu_config_t modbus_pending_config;
static app_modbus_master_device_runtime_t
    modbus_devices[APP_DEVICE_CONFIG_MAX_MASTER_DEVICES];
static app_modbus_command_entry_t
    modbus_commands[APP_MODBUS_COMMAND_QUEUE_SIZE];
static app_modbus_transaction_t modbus_active_transaction;
static app_modbus_rtu_diagnostics_t modbus_diagnostics;

static uint8_t modbus_command_head;
static uint8_t modbus_command_tail;
static uint8_t modbus_command_count;
static uint8_t modbus_command_burst;
static uint8_t modbus_poll_cursor;
static uint8_t modbus_config_pending;
static uint32_t modbus_next_command_id;
static volatile uint32_t modbus_received_bytes;
static uint8_t modbus_started;

static void app_modbus_task_entry(ULONG thread_input);
static void app_modbus_receive(const uint8_t *data,
                               uint16_t length,
                               void *argument);
static int app_modbus_send(void *user, const uint8_t *data, size_t length);
static uint32_t app_modbus_ldc_lock(void *user);
static void app_modbus_ldc_unlock(void *user, uint32_t state);
static ULONG app_modbus_ms_to_ticks(uint32_t milliseconds);
static uint32_t app_modbus_ticks_to_ms(ULONG ticks);
static uint8_t app_modbus_time_reached(ULONG now, ULONG deadline);
static uint8_t app_modbus_function_for_class(uint8_t register_class);
static uint8_t app_modbus_first_enabled_class(uint8_t device_index);
static void app_modbus_apply_pending_config(void);
static void app_modbus_reset_master_runtime(ULONG now);
static void app_modbus_clear_receive_queue(void);
static void app_modbus_apply_outputs(void);
static void app_modbus_update_slave_inputs(void);
static uint8_t app_modbus_select_due_poll(ULONG now,
                                          uint8_t *device_index,
                                          uint8_t *register_class);
static uint8_t app_modbus_take_command(
    app_modbus_command_entry_t *entry);
static uint8_t app_modbus_start_poll(uint8_t device_index,
                                     uint8_t register_class);
static uint8_t app_modbus_start_command(
    const app_modbus_command_entry_t *entry);
static uint8_t app_modbus_send_pdu(uint8_t unit_id,
                                   const uint8_t *pdu,
                                   size_t pdu_length);
static void app_modbus_master_run(ULONG now);
static void app_modbus_master_process_frames(ULONG now);
static void app_modbus_master_process_frame(const uint8_t *frame,
                                             size_t frame_length,
                                             ULONG now);
static void app_modbus_master_complete_success(
    const ld_modbus_adu_view_t *view,
    ULONG now);
static void app_modbus_master_complete_failure(ULONG now,
                                               uint8_t command_result,
                                               uint8_t exception_code);
static void app_modbus_device_success(uint8_t device_index,
                                      uint8_t register_class,
                                      ULONG now);
static void app_modbus_device_failure(uint8_t device_index,
                                      uint8_t register_class,
                                      ULONG now,
                                      uint8_t timeout);
static void app_modbus_schedule_class(uint8_t device_index,
                                      uint8_t register_class,
                                      ULONG now);
static void app_modbus_log_device_state(uint8_t device_index,
                                        uint8_t old_state,
                                        uint8_t new_state);
static void app_modbus_log_command(uint32_t command_id,
                                   uint8_t unit_id,
                                   uint8_t function,
                                   uint8_t result);

HAL_StatusTypeDef app_modbus_rtu_start(void)
{
    ldc_easy_config_t queue_config;
    app_device_config_t stored_config;
    uint8_t loaded_from_flash;
    UINT status;

    if (modbus_started != 0U)
    {
        return HAL_BUSY;
    }

    memset(&queue_config, 0, sizeof(queue_config));
    memset(&modbus_map, 0, sizeof(modbus_map));
    memset(&modbus_active_config, 0, sizeof(modbus_active_config));
    memset(&modbus_pending_config, 0, sizeof(modbus_pending_config));
    memset(&modbus_active_transaction, 0, sizeof(modbus_active_transaction));
    memset(&modbus_diagnostics, 0, sizeof(modbus_diagnostics));
    memset(modbus_coils, 0, sizeof(modbus_coils));
    memset(modbus_discrete_inputs, 0, sizeof(modbus_discrete_inputs));
    memset(modbus_holding_registers, 0, sizeof(modbus_holding_registers));
    memset(modbus_input_registers, 0, sizeof(modbus_input_registers));
    modbus_received_bytes = 0UL;
    modbus_next_command_id = 1UL;

    app_device_config_set_defaults(&stored_config);
    loaded_from_flash = 0U;
    if (app_device_config_load(&stored_config, &loaded_from_flash) != HAL_OK)
    {
        app_device_config_set_defaults(&stored_config);
    }
    modbus_active_config.persistent = stored_config;
    modbus_pending_config = modbus_active_config;

    queue_config.ring_buffer = modbus_ring;
    queue_config.ring_size = sizeof(modbus_ring);
    queue_config.packet_pool = modbus_packets;
    queue_config.packet_count = APP_MODBUS_PACKET_COUNT;
    queue_config.max_frame = LD_MODBUS_RTU_MAX_ADU_LENGTH;
    queue_config.timeout_us = APP_MODBUS_FRAME_TIMEOUT_US;
    queue_config.mode = LDC_MODE_PROTECT;
    queue_config.lock = app_modbus_ldc_lock;
    queue_config.unlock = app_modbus_ldc_unlock;
    if (!ldc_easy_init(&modbus_queue, &queue_config))
    {
        return HAL_ERROR;
    }

    modbus_map.coils = modbus_coils;
    modbus_map.coils_count = APP_MODBUS_COIL_COUNT;
    modbus_map.discrete_inputs = modbus_discrete_inputs;
    modbus_map.discrete_inputs_count = APP_MODBUS_DISCRETE_COUNT;
    modbus_map.holding_registers = modbus_holding_registers;
    modbus_map.holding_registers_count = APP_MODBUS_HOLDING_COUNT;
    modbus_map.input_registers = modbus_input_registers;
    modbus_map.input_registers_count = APP_MODBUS_INPUT_COUNT;
    modbus_input_registers[0] = 0x0750U;
    modbus_input_registers[1] = 0x0002U;

    if (ld_modbus_ldc_rtu_server_init(
            &modbus_server,
            &modbus_queue,
            modbus_active_config.persistent.modbus_unit_id,
            &modbus_map,
            app_modbus_send,
            NULL,
            modbus_request,
            sizeof(modbus_request),
            modbus_response,
            sizeof(modbus_response)) != LD_MODBUS_STATUS_OK)
    {
        return HAL_ERROR;
    }
    if (bsp_rs485_receive_start(app_modbus_receive, NULL) != HAL_OK)
    {
        return HAL_ERROR;
    }

    status = tx_mutex_create(&modbus_state_mutex,
                             "modbus_state",
                             TX_INHERIT);
    if (status != TX_SUCCESS)
    {
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }
    status = tx_event_flags_create(&modbus_control_events,
                                   "modbus_control");
    if (status != TX_SUCCESS)
    {
        (void)tx_mutex_delete(&modbus_state_mutex);
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }

    app_modbus_reset_master_runtime(tx_time_get());
    status = tx_thread_create(&modbus_task_control_block,
                              "modbus_rtu_task",
                              app_modbus_task_entry,
                              0UL,
                              modbus_task_stack,
                              sizeof(modbus_task_stack),
                              APP_MODBUS_TASK_PRIORITY,
                              APP_MODBUS_TASK_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_DONT_START);
    if (status != TX_SUCCESS)
    {
        (void)tx_event_flags_delete(&modbus_control_events);
        (void)tx_mutex_delete(&modbus_state_mutex);
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }

    modbus_started = 1U;
    if (tx_thread_resume(&modbus_task_control_block) != TX_SUCCESS)
    {
        modbus_started = 0U;
        (void)tx_thread_delete(&modbus_task_control_block);
        (void)tx_event_flags_delete(&modbus_control_events);
        (void)tx_mutex_delete(&modbus_state_mutex);
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }
    return HAL_OK;
}

HAL_StatusTypeDef app_modbus_rtu_request_config(
    const app_modbus_rtu_config_t *config)
{
    if ((config == NULL) ||
        (config->red_led_on > 1U) ||
        (config->buzzer_on > 1U) ||
        (app_device_config_validate(&config->persistent) == 0U))
    {
        return HAL_ERROR;
    }
    if (modbus_started == 0U)
    {
        return HAL_BUSY;
    }

    if (tx_mutex_get(&modbus_state_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    modbus_pending_config = *config;
    modbus_config_pending = 1U;
    (void)tx_mutex_put(&modbus_state_mutex);

    return (tx_event_flags_set(&modbus_control_events,
                               APP_MODBUS_EVENT_CONFIG_PENDING,
                               TX_OR) == TX_SUCCESS) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef app_modbus_rtu_get_config(app_modbus_rtu_config_t *config)
{
    if ((config == NULL) || (modbus_started == 0U))
    {
        return HAL_ERROR;
    }
    if (tx_mutex_get(&modbus_state_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    *config = modbus_active_config;
    (void)tx_mutex_put(&modbus_state_mutex);
    return HAL_OK;
}

HAL_StatusTypeDef app_modbus_rtu_queue_command(
    const app_modbus_command_t *command,
    uint32_t *command_id)
{
    app_modbus_command_entry_t *entry;

    if ((command == NULL) || (command_id == NULL) ||
        (command->type > APP_MODBUS_COMMAND_WRITE_REGISTER) ||
        ((command->type == APP_MODBUS_COMMAND_WRITE_COIL) &&
         (command->value > 1U)) ||
        (modbus_started == 0U))
    {
        return HAL_ERROR;
    }
    if (tx_mutex_get(&modbus_state_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if ((modbus_active_config.persistent.rs485_role !=
         APP_RS485_ROLE_MASTER) ||
        (command->device_index >=
         modbus_active_config.persistent.master_device_count) ||
        (modbus_command_count >= APP_MODBUS_COMMAND_QUEUE_SIZE))
    {
        (void)tx_mutex_put(&modbus_state_mutex);
        return HAL_BUSY;
    }

    entry = &modbus_commands[modbus_command_tail];
    entry->command = *command;
    entry->id = modbus_next_command_id++;
    if (modbus_next_command_id == 0UL)
    {
        modbus_next_command_id = 1UL;
    }
    modbus_command_tail++;
    if (modbus_command_tail >= APP_MODBUS_COMMAND_QUEUE_SIZE)
    {
        modbus_command_tail = 0U;
    }
    modbus_command_count++;
    modbus_diagnostics.commands_queued++;
    modbus_diagnostics.last_command_id = entry->id;
    modbus_diagnostics.last_command_result =
        APP_MODBUS_COMMAND_RESULT_QUEUED;
    *command_id = entry->id;
    (void)tx_mutex_put(&modbus_state_mutex);
    return HAL_OK;
}

HAL_StatusTypeDef app_modbus_rtu_get_diagnostics(
    app_modbus_rtu_diagnostics_t *diagnostics)
{
    ld_modbus_ldc_diagnostics_t server_diagnostics;
    ldc_stats_t ldc_stats;
    uint32_t device_index;

    if ((diagnostics == NULL) || (modbus_started == 0U))
    {
        return HAL_ERROR;
    }
    if (tx_mutex_get(&modbus_state_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }

    *diagnostics = modbus_diagnostics;
    diagnostics->role = modbus_active_config.persistent.rs485_role;
    diagnostics->device_count =
        modbus_active_config.persistent.master_device_count;
    diagnostics->command_queue_depth = modbus_command_count;
    diagnostics->active_transaction =
        (modbus_active_transaction.stage != APP_MODBUS_TRANSACTION_IDLE) ?
        1U : 0U;
    if (diagnostics->active_transaction != 0U)
    {
        uint8_t index = modbus_active_transaction.device_index;

        diagnostics->active_unit_id =
            modbus_active_config.persistent.master_devices[index].unit_id;
        diagnostics->active_function = modbus_active_transaction.function;
    }
    diagnostics->received_bytes = modbus_received_bytes;
    for (device_index = 0U;
         device_index < APP_DEVICE_CONFIG_MAX_MASTER_DEVICES;
         device_index++)
    {
        diagnostics->devices[device_index] =
            modbus_devices[device_index].status;
    }
    (void)tx_mutex_put(&modbus_state_mutex);

    if (ldc_easy_get_stats(&modbus_queue, &ldc_stats))
    {
        diagnostics->ldc_overflow = ldc_stats.overflow;
        diagnostics->ldc_drop = ldc_stats.drop;
    }
    if ((diagnostics->role == APP_RS485_ROLE_SLAVE) &&
        (ld_modbus_ldc_rtu_server_get_diagnostics(
            &modbus_server,
            &server_diagnostics) == LD_MODBUS_STATUS_OK))
    {
        diagnostics->received_frames = server_diagnostics.received_frames;
        diagnostics->replied_frames = server_diagnostics.replied_frames;
        diagnostics->crc_errors = server_diagnostics.crc_errors;
        diagnostics->malformed_frames = server_diagnostics.malformed_frames;
    }
    return HAL_OK;
}

const char *app_modbus_device_state_name(uint8_t state)
{
    switch (state)
    {
        case APP_MODBUS_DEVICE_ONLINE:
            return "online";
        case APP_MODBUS_DEVICE_SUSPECT:
            return "suspect";
        case APP_MODBUS_DEVICE_OFFLINE:
            return "offline";
        case APP_MODBUS_DEVICE_PROBING:
            return "probing";
        default:
            return "unknown";
    }
}

const char *app_modbus_command_result_name(uint8_t result)
{
    switch (result)
    {
        case APP_MODBUS_COMMAND_RESULT_NONE:
            return "none";
        case APP_MODBUS_COMMAND_RESULT_QUEUED:
            return "queued";
        case APP_MODBUS_COMMAND_RESULT_OK:
            return "ok";
        case APP_MODBUS_COMMAND_RESULT_TIMEOUT:
            return "timeout";
        case APP_MODBUS_COMMAND_RESULT_EXCEPTION:
            return "exception";
        case APP_MODBUS_COMMAND_RESULT_PROTOCOL_ERROR:
            return "protocol_error";
        case APP_MODBUS_COMMAND_RESULT_CANCELLED:
            return "cancelled";
        default:
            return "unknown";
    }
}

static void app_modbus_task_entry(ULONG thread_input)
{
    uint32_t last_cycles = bsp_dwt_get_cycles();
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;

    (void)thread_input;
    if (cycles_per_us == 0U)
    {
        cycles_per_us = 1U;
    }

    while (1)
    {
        ULONG actual_flags;
        uint32_t current_cycles = bsp_dwt_get_cycles();
        uint32_t elapsed_cycles = current_cycles - last_cycles;
        uint32_t elapsed_us = elapsed_cycles / cycles_per_us;
        ULONG now = tx_time_get();

        if (tx_event_flags_get(&modbus_control_events,
                               APP_MODBUS_EVENT_CONFIG_PENDING,
                               TX_OR_CLEAR,
                               &actual_flags,
                               TX_NO_WAIT) == TX_SUCCESS)
        {
            (void)actual_flags;
        }
        if ((modbus_config_pending != 0U) &&
            (modbus_active_transaction.stage ==
             APP_MODBUS_TRANSACTION_IDLE))
        {
            app_modbus_apply_pending_config();
        }

        if (elapsed_us != 0U)
        {
            last_cycles += elapsed_us * cycles_per_us;
            ldc_easy_tick_us(&modbus_queue, elapsed_us);
        }

        if (modbus_active_config.persistent.rs485_role ==
            APP_RS485_ROLE_SLAVE)
        {
            uint8_t did_work;

            do
            {
                did_work = 0U;
                (void)ld_modbus_ldc_rtu_server_poll(&modbus_server,
                                                    &did_work);
                if (did_work != 0U)
                {
                    app_modbus_apply_outputs();
                    app_modbus_update_slave_inputs();
                }
            } while (did_work != 0U);
        }
        else
        {
            app_modbus_master_run(now);
        }

        (void)tx_thread_sleep(1U);
    }
}

static void app_modbus_receive(const uint8_t *data,
                               uint16_t length,
                               void *argument)
{
    (void)argument;
    modbus_received_bytes += length;
    (void)ldc_easy_add(&modbus_queue, data, length);
}

static int app_modbus_send(void *user, const uint8_t *data, size_t length)
{
    (void)user;
    return (int)bsp_rs485_write(data, length);
}

static uint32_t app_modbus_ldc_lock(void *user)
{
    uint32_t interrupt_state = __get_PRIMASK();

    (void)user;
    if ((__get_IPSR() == 0U) && (interrupt_state == 0U))
    {
        __disable_irq();
        __DMB();
    }
    return interrupt_state;
}

static void app_modbus_ldc_unlock(void *user, uint32_t interrupt_state)
{
    (void)user;
    if ((__get_IPSR() == 0U) && (interrupt_state == 0U))
    {
        __DMB();
        __enable_irq();
    }
}

static ULONG app_modbus_ms_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks =
        ((uint64_t)milliseconds * TX_TIMER_TICKS_PER_SECOND + 999ULL) /
        1000ULL;

    return (ticks == 0ULL) ? 1UL : (ULONG)ticks;
}

static uint32_t app_modbus_ticks_to_ms(ULONG ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) /
                      TX_TIMER_TICKS_PER_SECOND);
}

static uint8_t app_modbus_time_reached(ULONG now, ULONG deadline)
{
    return ((LONG)(now - deadline) >= 0L) ? 1U : 0U;
}

static uint8_t app_modbus_function_for_class(uint8_t register_class)
{
    static const uint8_t functions[] =
    {
        LD_MODBUS_FC_READ_COILS,
        LD_MODBUS_FC_READ_DISCRETE_INPUTS,
        LD_MODBUS_FC_READ_HOLDING_REGISTERS,
        LD_MODBUS_FC_READ_INPUT_REGISTERS
    };

    return (register_class < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT) ?
           functions[register_class] : 0U;
}

static uint8_t app_modbus_first_enabled_class(uint8_t device_index)
{
    uint8_t register_class;

    for (register_class = 0U;
         register_class < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
         register_class++)
    {
        if (modbus_active_config.persistent
                .master_devices[device_index]
                .ranges[register_class]
                .quantity != 0U)
        {
            return register_class;
        }
    }
    return 0U;
}

static void app_modbus_apply_pending_config(void)
{
    app_modbus_rtu_config_t config;
    char message[128];

    if (tx_mutex_get(&modbus_state_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return;
    }
    config = modbus_pending_config;
    modbus_active_config = config;
    modbus_config_pending = 0U;
    modbus_server.unit_id = config.persistent.modbus_unit_id;
    modbus_coils[0] = config.red_led_on;
    modbus_coils[1] = config.buzzer_on;
    modbus_command_head = 0U;
    modbus_command_tail = 0U;
    modbus_command_count = 0U;
    modbus_command_burst = 0U;
    memset(&modbus_active_transaction, 0, sizeof(modbus_active_transaction));
    app_modbus_reset_master_runtime(tx_time_get());
    (void)tx_mutex_put(&modbus_state_mutex);

    app_modbus_clear_receive_queue();
    app_modbus_apply_outputs();
    (void)snprintf(message,
                   sizeof(message),
                   "RS485 mode applied: %s, slave_unit=%u, master_devices=%u\r\n",
                   (config.persistent.rs485_role ==
                    APP_RS485_ROLE_MASTER) ? "master" : "slave",
                   (unsigned int)config.persistent.modbus_unit_id,
                   (unsigned int)config.persistent.master_device_count);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}

static void app_modbus_reset_master_runtime(ULONG now)
{
    uint32_t device_index;
    uint32_t total_slots =
        (uint32_t)modbus_active_config.persistent.master_device_count *
        APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
    uint32_t spacing_ms = (total_slots == 0U) ?
                          APP_DEVICE_CONFIG_DEFAULT_POLL_PERIOD_MS :
                          APP_DEVICE_CONFIG_DEFAULT_POLL_PERIOD_MS /
                          total_slots;

    memset(modbus_devices, 0, sizeof(modbus_devices));
    memset(&modbus_diagnostics.devices,
           0,
           sizeof(modbus_diagnostics.devices));
    if (spacing_ms == 0U)
    {
        spacing_ms = 1U;
    }

    for (device_index = 0U;
         device_index < APP_DEVICE_CONFIG_MAX_MASTER_DEVICES;
         device_index++)
    {
        uint32_t register_class;
        app_modbus_master_device_runtime_t *runtime =
            &modbus_devices[device_index];

        runtime->status.unit_id =
            modbus_active_config.persistent
                .master_devices[device_index]
                .unit_id;
        runtime->status.state = APP_MODBUS_DEVICE_ONLINE;
        for (register_class = 0U;
             register_class < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
             register_class++)
        {
            uint32_t slot =
                register_class *
                modbus_active_config.persistent.master_device_count +
                device_index;

            runtime->next_due[register_class] =
                now + app_modbus_ms_to_ticks(slot * spacing_ms);
        }
        runtime->status.next_action_ms =
            app_modbus_ticks_to_ms(runtime->next_due[0]);
    }
    modbus_poll_cursor = 0U;
}

static void app_modbus_clear_receive_queue(void)
{
    uint8_t discarded[LD_MODBUS_RTU_MAX_ADU_LENGTH];

    while (ldc_easy_pop(&modbus_queue,
                        discarded,
                        sizeof(discarded)) > 0)
    {
    }
    (void)ldc_easy_abort(&modbus_queue);
}

static void app_modbus_apply_outputs(void)
{
    if (modbus_coils[0] != 0U)
    {
        bsp_led_on(BSP_LED_RED);
    }
    else
    {
        bsp_led_off(BSP_LED_RED);
    }
    if (modbus_coils[1] != 0U)
    {
        bsp_beep_on();
    }
    else
    {
        bsp_beep_off();
    }

    if (tx_mutex_get(&modbus_state_mutex, TX_NO_WAIT) == TX_SUCCESS)
    {
        modbus_active_config.red_led_on =
            (modbus_coils[0] != 0U) ? 1U : 0U;
        modbus_active_config.buzzer_on =
            (modbus_coils[1] != 0U) ? 1U : 0U;
        (void)tx_mutex_put(&modbus_state_mutex);
    }
}

static void app_modbus_update_slave_inputs(void)
{
    ld_modbus_ldc_diagnostics_t diagnostics;
    ldc_stats_t ldc_stats;

    if (ld_modbus_ldc_rtu_server_get_diagnostics(
            &modbus_server,
            &diagnostics) == LD_MODBUS_STATUS_OK)
    {
        modbus_input_registers[2] =
            (uint16_t)diagnostics.received_frames;
        modbus_input_registers[3] =
            (uint16_t)diagnostics.replied_frames;
        modbus_input_registers[4] =
            (uint16_t)diagnostics.crc_errors;
        modbus_input_registers[5] =
            (uint16_t)diagnostics.malformed_frames;
    }
    if (ldc_easy_get_stats(&modbus_queue, &ldc_stats))
    {
        modbus_input_registers[6] = (uint16_t)ldc_stats.overflow;
        modbus_input_registers[7] = (uint16_t)ldc_stats.drop;
    }
}

static uint8_t app_modbus_select_due_poll(ULONG now,
                                          uint8_t *device_index,
                                          uint8_t *register_class)
{
    uint8_t count =
        modbus_active_config.persistent.master_device_count;
    uint8_t index;
    uint32_t total_slots = (uint32_t)count *
                           APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
    uint32_t offset;

    for (index = 0U; index < count; index++)
    {
        app_modbus_master_device_runtime_t *runtime =
            &modbus_devices[index];

        if ((runtime->status.state == APP_MODBUS_DEVICE_OFFLINE) &&
            (app_modbus_time_reached(now,
                                     runtime->next_due[0]) != 0U))
        {
            runtime->status.state = APP_MODBUS_DEVICE_PROBING;
            *device_index = index;
            *register_class = app_modbus_first_enabled_class(index);
            return 1U;
        }
    }

    if (total_slots == 0U)
    {
        return 0U;
    }
    for (offset = 0U; offset < total_slots; offset++)
    {
        uint32_t slot = (modbus_poll_cursor + offset) % total_slots;
        uint8_t local_class = (uint8_t)(slot / count);
        uint8_t local_device = (uint8_t)(slot % count);
        app_modbus_master_device_runtime_t *runtime =
            &modbus_devices[local_device];
        const app_modbus_poll_range_t *range =
            &modbus_active_config.persistent
                 .master_devices[local_device]
                 .ranges[local_class];

        if ((runtime->status.state != APP_MODBUS_DEVICE_OFFLINE) &&
            (runtime->status.state != APP_MODBUS_DEVICE_PROBING) &&
            (range->quantity != 0U) &&
            (app_modbus_time_reached(
                now,
                runtime->next_due[local_class]) != 0U))
        {
            modbus_poll_cursor = (uint8_t)((slot + 1U) % total_slots);
            *device_index = local_device;
            *register_class = local_class;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t app_modbus_take_command(
    app_modbus_command_entry_t *entry)
{
    if (modbus_command_count == 0U)
    {
        return 0U;
    }
    *entry = modbus_commands[modbus_command_head];
    modbus_command_head++;
    if (modbus_command_head >= APP_MODBUS_COMMAND_QUEUE_SIZE)
    {
        modbus_command_head = 0U;
    }
    modbus_command_count--;
    return 1U;
}

static uint8_t app_modbus_start_poll(uint8_t device_index,
                                     uint8_t register_class)
{
    const app_modbus_master_device_config_t *device =
        &modbus_active_config.persistent.master_devices[device_index];
    const app_modbus_poll_range_t *range =
        &device->ranges[register_class];
    size_t pdu_length;
    uint8_t function = app_modbus_function_for_class(register_class);

    if (ld_modbus_client_build_read_request(function,
                                            range->address,
                                            range->quantity,
                                            modbus_pdu,
                                            sizeof(modbus_pdu),
                                            &pdu_length) !=
        LD_MODBUS_STATUS_OK)
    {
        return 0U;
    }

    memset(&modbus_active_transaction,
           0,
           sizeof(modbus_active_transaction));
    modbus_active_transaction.kind = APP_MODBUS_TRANSACTION_POLL;
    modbus_active_transaction.device_index = device_index;
    modbus_active_transaction.register_class = register_class;
    modbus_active_transaction.function = function;
    modbus_active_transaction.address = range->address;
    modbus_active_transaction.quantity = range->quantity;
    return app_modbus_send_pdu(device->unit_id,
                               modbus_pdu,
                               pdu_length);
}

static uint8_t app_modbus_start_command(
    const app_modbus_command_entry_t *entry)
{
    const app_modbus_master_device_config_t *device =
        &modbus_active_config.persistent
             .master_devices[entry->command.device_index];
    size_t pdu_length;
    ld_modbus_status_t status;
    uint8_t function;

    if (entry->command.type == APP_MODBUS_COMMAND_WRITE_COIL)
    {
        function = LD_MODBUS_FC_WRITE_SINGLE_COIL;
        status = ld_modbus_client_build_write_single_coil(
            entry->command.address,
            (uint8_t)entry->command.value,
            modbus_pdu,
            sizeof(modbus_pdu),
            &pdu_length);
    }
    else
    {
        function = LD_MODBUS_FC_WRITE_SINGLE_REGISTER;
        status = ld_modbus_client_build_write_single_register(
            entry->command.address,
            entry->command.value,
            modbus_pdu,
            sizeof(modbus_pdu),
            &pdu_length);
    }
    if (status != LD_MODBUS_STATUS_OK)
    {
        return 0U;
    }

    memset(&modbus_active_transaction,
           0,
           sizeof(modbus_active_transaction));
    modbus_active_transaction.kind = APP_MODBUS_TRANSACTION_COMMAND;
    modbus_active_transaction.device_index =
        entry->command.device_index;
    modbus_active_transaction.function = function;
    modbus_active_transaction.address = entry->command.address;
    modbus_active_transaction.value = entry->command.value;
    modbus_active_transaction.command_id = entry->id;
    return app_modbus_send_pdu(device->unit_id,
                               modbus_pdu,
                               pdu_length);
}

static uint8_t app_modbus_send_pdu(uint8_t unit_id,
                                   const uint8_t *pdu,
                                   size_t pdu_length)
{
    size_t frame_length;

    app_modbus_clear_receive_queue();
    if (ld_modbus_rtu_encode(unit_id,
                             pdu,
                             pdu_length,
                             modbus_frame,
                             sizeof(modbus_frame),
                             &frame_length) != LD_MODBUS_STATUS_OK)
    {
        memset(&modbus_active_transaction,
               0,
               sizeof(modbus_active_transaction));
        return 0U;
    }
    if (bsp_rs485_write(modbus_frame, frame_length) != frame_length)
    {
        memset(&modbus_active_transaction,
               0,
               sizeof(modbus_active_transaction));
        return 0U;
    }
    modbus_active_transaction.stage = APP_MODBUS_TRANSACTION_TX_DRAIN;
    return 1U;
}

static void app_modbus_master_run(ULONG now)
{
    uint8_t due_device = 0U;
    uint8_t due_class = 0U;
    uint8_t poll_due;

    if (modbus_active_transaction.stage ==
        APP_MODBUS_TRANSACTION_TX_DRAIN)
    {
        if (bsp_rs485_tx_empty() != 0U)
        {
            uint16_t timeout_ms =
                modbus_active_config.persistent
                    .master_devices[
                        modbus_active_transaction.device_index]
                    .response_timeout_ms;

            modbus_active_transaction.deadline =
                now + app_modbus_ms_to_ticks(timeout_ms);
            modbus_active_transaction.stage =
                APP_MODBUS_TRANSACTION_WAIT_RESPONSE;
        }
        return;
    }

    if (modbus_active_transaction.stage ==
        APP_MODBUS_TRANSACTION_WAIT_RESPONSE)
    {
        app_modbus_master_process_frames(now);
        if ((modbus_active_transaction.stage !=
             APP_MODBUS_TRANSACTION_IDLE) &&
            (app_modbus_time_reached(
                now,
                modbus_active_transaction.deadline) != 0U))
        {
            app_modbus_master_complete_failure(
                now,
                APP_MODBUS_COMMAND_RESULT_TIMEOUT,
                0U);
        }
        return;
    }

    poll_due = app_modbus_select_due_poll(now,
                                          &due_device,
                                          &due_class);
    if (tx_mutex_get(&modbus_state_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return;
    }
    if ((modbus_command_count != 0U) &&
        ((modbus_command_burst < APP_MODBUS_MAX_COMMAND_BURST) ||
         (poll_due == 0U)))
    {
        app_modbus_command_entry_t entry;

        if (app_modbus_take_command(&entry) != 0U)
        {
            if (poll_due != 0U)
            {
                modbus_diagnostics.priority_dispatches++;
            }
            modbus_command_burst++;
            (void)tx_mutex_put(&modbus_state_mutex);
            if (app_modbus_start_command(&entry) == 0U)
            {
                app_modbus_log_command(
                    entry.id,
                    modbus_active_config.persistent
                        .master_devices[entry.command.device_index]
                        .unit_id,
                    (entry.command.type ==
                     APP_MODBUS_COMMAND_WRITE_COIL) ?
                     LD_MODBUS_FC_WRITE_SINGLE_COIL :
                     LD_MODBUS_FC_WRITE_SINGLE_REGISTER,
                    APP_MODBUS_COMMAND_RESULT_PROTOCOL_ERROR);
            }
            return;
        }
    }
    (void)tx_mutex_put(&modbus_state_mutex);

    if (poll_due != 0U)
    {
        modbus_command_burst = 0U;
        if (app_modbus_start_poll(due_device, due_class) == 0U)
        {
            app_modbus_schedule_class(due_device,
                                      due_class,
                                      now);
        }
    }
}

static void app_modbus_master_process_frames(ULONG now)
{
    int32_t length;

    do
    {
        length = ldc_easy_pop(&modbus_queue,
                              modbus_response,
                              sizeof(modbus_response));
        if (length > 0)
        {
            modbus_diagnostics.received_frames++;
            app_modbus_master_process_frame(modbus_response,
                                            (size_t)length,
                                            now);
        }
    } while ((length > 0) &&
             (modbus_active_transaction.stage !=
              APP_MODBUS_TRANSACTION_IDLE));
}

static void app_modbus_master_process_frame(const uint8_t *frame,
                                             size_t frame_length,
                                             ULONG now)
{
    ld_modbus_adu_view_t view;
    ld_modbus_status_t status =
        ld_modbus_rtu_decode(frame, frame_length, &view);
    uint8_t expected_unit =
        modbus_active_config.persistent
            .master_devices[modbus_active_transaction.device_index]
            .unit_id;

    if (status == LD_MODBUS_STATUS_BAD_CRC)
    {
        modbus_diagnostics.crc_errors++;
        return;
    }
    if (status != LD_MODBUS_STATUS_OK)
    {
        modbus_diagnostics.malformed_frames++;
        return;
    }
    if (view.unit_id != expected_unit)
    {
        return;
    }
    app_modbus_master_complete_success(&view, now);
}

static void app_modbus_master_complete_success(
    const ld_modbus_adu_view_t *view,
    ULONG now)
{
    app_modbus_transaction_t transaction =
        modbus_active_transaction;
    app_modbus_master_device_runtime_t *runtime =
        &modbus_devices[transaction.device_index];
    ld_modbus_status_t status;
    uint8_t exception = 0U;

    if (transaction.kind == APP_MODBUS_TRANSACTION_POLL)
    {
        if ((transaction.function == LD_MODBUS_FC_READ_COILS) ||
            (transaction.function ==
             LD_MODBUS_FC_READ_DISCRETE_INPUTS))
        {
            uint8_t values[APP_DEVICE_CONFIG_MAX_RANGE_QUANTITY];
            uint32_t index;

            status = ld_modbus_client_parse_read_bits_response(
                transaction.function,
                transaction.quantity,
                view->pdu,
                view->pdu_length,
                values,
                sizeof(values),
                &exception);
            if (status == LD_MODBUS_STATUS_OK)
            {
                for (index = 0U; index < transaction.quantity; index++)
                {
                    runtime->status
                        .values[transaction.register_class][index] =
                        values[index];
                }
            }
        }
        else
        {
            status = ld_modbus_client_parse_read_registers_response(
                transaction.function,
                transaction.quantity,
                view->pdu,
                view->pdu_length,
                runtime->status.values[transaction.register_class],
                APP_DEVICE_CONFIG_MAX_RANGE_QUANTITY,
                &exception);
        }

        if (status == LD_MODBUS_STATUS_OK)
        {
            runtime->status.successful_polls++;
            modbus_diagnostics.polls_completed++;
            app_modbus_device_success(transaction.device_index,
                                      transaction.register_class,
                                      now);
            memset(&modbus_active_transaction,
                   0,
                   sizeof(modbus_active_transaction));
        }
        else if (status == LD_MODBUS_STATUS_EXCEPTION_RESPONSE)
        {
            runtime->status.last_exception = exception;
            runtime->status.protocol_errors++;
            app_modbus_device_success(transaction.device_index,
                                      transaction.register_class,
                                      now);
            memset(&modbus_active_transaction,
                   0,
                   sizeof(modbus_active_transaction));
        }
        else
        {
            app_modbus_master_complete_failure(
                now,
                APP_MODBUS_COMMAND_RESULT_PROTOCOL_ERROR,
                exception);
        }
        return;
    }

    status = ld_modbus_client_parse_write_response(
        transaction.function,
        transaction.address,
        (transaction.function == LD_MODBUS_FC_WRITE_SINGLE_COIL) ?
            ((transaction.value != 0U) ? 0xFF00U : 0x0000U) :
            transaction.value,
        view->pdu,
        view->pdu_length,
        &exception);
    if (status == LD_MODBUS_STATUS_OK)
    {
        app_modbus_device_success(transaction.device_index, 0U, now);
        modbus_diagnostics.commands_completed++;
        modbus_diagnostics.last_command_result =
            APP_MODBUS_COMMAND_RESULT_OK;
        modbus_diagnostics.last_command_id = transaction.command_id;
        modbus_diagnostics.last_command_unit_id =
            runtime->status.unit_id;
        modbus_diagnostics.last_command_function =
            transaction.function;
        modbus_diagnostics.last_command_exception = 0U;
        app_modbus_log_command(transaction.command_id,
                               runtime->status.unit_id,
                               transaction.function,
                               APP_MODBUS_COMMAND_RESULT_OK);
        memset(&modbus_active_transaction,
               0,
               sizeof(modbus_active_transaction));
    }
    else
    {
        app_modbus_master_complete_failure(
            now,
            (status == LD_MODBUS_STATUS_EXCEPTION_RESPONSE) ?
                APP_MODBUS_COMMAND_RESULT_EXCEPTION :
                APP_MODBUS_COMMAND_RESULT_PROTOCOL_ERROR,
            exception);
    }
}

static void app_modbus_master_complete_failure(ULONG now,
                                               uint8_t command_result,
                                               uint8_t exception_code)
{
    app_modbus_transaction_t transaction =
        modbus_active_transaction;
    uint8_t device_index = transaction.device_index;
    app_modbus_master_device_runtime_t *runtime =
        &modbus_devices[device_index];

    if (transaction.kind == APP_MODBUS_TRANSACTION_POLL)
    {
        if (command_result == APP_MODBUS_COMMAND_RESULT_TIMEOUT)
        {
            modbus_diagnostics.poll_timeouts++;
        }
        else
        {
            runtime->status.protocol_errors++;
        }
        app_modbus_device_failure(
            device_index,
            transaction.register_class,
            now,
            (command_result ==
             APP_MODBUS_COMMAND_RESULT_TIMEOUT) ? 1U : 0U);
    }
    else if (transaction.kind == APP_MODBUS_TRANSACTION_COMMAND)
    {
        modbus_diagnostics.commands_failed++;
        modbus_diagnostics.last_command_id = transaction.command_id;
        modbus_diagnostics.last_command_result = command_result;
        modbus_diagnostics.last_command_unit_id =
            runtime->status.unit_id;
        modbus_diagnostics.last_command_function =
            transaction.function;
        modbus_diagnostics.last_command_exception = exception_code;
        app_modbus_log_command(transaction.command_id,
                               runtime->status.unit_id,
                               transaction.function,
                               command_result);
        if (command_result == APP_MODBUS_COMMAND_RESULT_TIMEOUT)
        {
            app_modbus_device_failure(device_index,
                                      0U,
                                      now,
                                      1U);
        }
    }
    memset(&modbus_active_transaction,
           0,
           sizeof(modbus_active_transaction));
}

static void app_modbus_device_success(uint8_t device_index,
                                      uint8_t register_class,
                                      ULONG now)
{
    app_modbus_master_device_runtime_t *runtime =
        &modbus_devices[device_index];
    uint8_t old_state = runtime->status.state;

    runtime->status.state = APP_MODBUS_DEVICE_ONLINE;
    runtime->status.consecutive_failures = 0U;
    runtime->status.backoff_step = 0U;
    runtime->status.last_function =
        modbus_active_transaction.function;
    runtime->status.last_success_ms = app_modbus_ticks_to_ms(now);
    app_modbus_schedule_class(device_index, register_class, now);

    if ((old_state == APP_MODBUS_DEVICE_OFFLINE) ||
        (old_state == APP_MODBUS_DEVICE_PROBING))
    {
        uint8_t local_class;

        for (local_class = 0U;
             local_class < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
             local_class++)
        {
            modbus_devices[device_index].next_due[local_class] =
                now + app_modbus_ms_to_ticks(
                    (uint32_t)local_class * 10UL);
        }
        app_modbus_log_device_state(device_index,
                                    old_state,
                                    APP_MODBUS_DEVICE_ONLINE);
    }
}

static void app_modbus_device_failure(uint8_t device_index,
                                      uint8_t register_class,
                                      ULONG now,
                                      uint8_t timeout)
{
    app_modbus_master_device_runtime_t *runtime =
        &modbus_devices[device_index];
    uint8_t old_state = runtime->status.state;

    runtime->status.last_function =
        modbus_active_transaction.function;
    if (timeout != 0U)
    {
        runtime->status.timeouts++;
    }

    if ((old_state == APP_MODBUS_DEVICE_OFFLINE) ||
        (old_state == APP_MODBUS_DEVICE_PROBING))
    {
        uint32_t delay_ms;

        if (runtime->status.backoff_step <
            (sizeof(modbus_backoff_ms) /
             sizeof(modbus_backoff_ms[0])))
        {
            delay_ms =
                modbus_backoff_ms[runtime->status.backoff_step];
            runtime->status.backoff_step++;
        }
        else
        {
            delay_ms =
                (uint32_t)modbus_active_config.persistent
                    .offline_probe_period_s * 1000UL;
        }
        runtime->status.state = APP_MODBUS_DEVICE_OFFLINE;
        runtime->next_due[0] =
            now + app_modbus_ms_to_ticks(delay_ms);
        runtime->status.next_action_ms =
            app_modbus_ticks_to_ms(runtime->next_due[0]);
        if (old_state != APP_MODBUS_DEVICE_OFFLINE)
        {
            app_modbus_log_device_state(
                device_index,
                old_state,
                APP_MODBUS_DEVICE_OFFLINE);
        }
        return;
    }

    if (runtime->status.consecutive_failures < 0xFFU)
    {
        runtime->status.consecutive_failures++;
    }
    if (runtime->status.consecutive_failures >=
        APP_MODBUS_FAILURES_BEFORE_OFFLINE)
    {
        runtime->status.state = APP_MODBUS_DEVICE_OFFLINE;
        runtime->status.backoff_step = 1U;
        runtime->next_due[0] =
            now + app_modbus_ms_to_ticks(modbus_backoff_ms[0]);
        runtime->status.next_action_ms =
            app_modbus_ticks_to_ms(runtime->next_due[0]);
        app_modbus_log_device_state(device_index,
                                    old_state,
                                    APP_MODBUS_DEVICE_OFFLINE);
    }
    else
    {
        runtime->status.state = APP_MODBUS_DEVICE_SUSPECT;
        app_modbus_schedule_class(device_index, register_class, now);
        if (old_state != APP_MODBUS_DEVICE_SUSPECT)
        {
            app_modbus_log_device_state(device_index,
                                        old_state,
                                        APP_MODBUS_DEVICE_SUSPECT);
        }
    }
}

static void app_modbus_schedule_class(uint8_t device_index,
                                      uint8_t register_class,
                                      ULONG now)
{
    ULONG next =
        now + app_modbus_ms_to_ticks(
            modbus_active_config.persistent.poll_period_ms);

    modbus_devices[device_index].next_due[register_class] = next;
    modbus_devices[device_index].status.next_action_ms =
        app_modbus_ticks_to_ms(next);
}

static void app_modbus_log_device_state(uint8_t device_index,
                                        uint8_t old_state,
                                        uint8_t new_state)
{
    char message[128];

    (void)snprintf(
        message,
        sizeof(message),
        "Modbus master device unit=%u: %s -> %s, failures=%u, backoff=%u\r\n",
        (unsigned int)modbus_devices[device_index].status.unit_id,
        app_modbus_device_state_name(old_state),
        app_modbus_device_state_name(new_state),
        (unsigned int)modbus_devices[device_index]
            .status.consecutive_failures,
        (unsigned int)modbus_devices[device_index]
            .status.backoff_step);
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}

static void app_modbus_log_command(uint32_t command_id,
                                   uint8_t unit_id,
                                   uint8_t function,
                                   uint8_t result)
{
    char message[112];

    (void)snprintf(message,
                   sizeof(message),
                   "Modbus command id=%lu unit=%u fc=%02X result=%s\r\n",
                   (unsigned long)command_id,
                   (unsigned int)unit_id,
                   (unsigned int)function,
                   app_modbus_command_result_name(result));
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}
