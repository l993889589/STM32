#include "app_modbus_rtu.h"

#include <string.h>

#include "app_device_config.h"
#include "bsp.h"
#include "ld_modbus_ldc.h"
#include "tx_api.h"

#define APP_MODBUS_TASK_PRIORITY         4U
#define APP_MODBUS_TASK_STACK_SIZE    2048U
#define APP_MODBUS_FRAME_TIMEOUT_US    1750U
#define APP_MODBUS_PACKET_COUNT           4U
#define APP_MODBUS_COIL_COUNT             16U
#define APP_MODBUS_DISCRETE_COUNT         16U
#define APP_MODBUS_HOLDING_COUNT          16U
#define APP_MODBUS_INPUT_COUNT            16U
#define APP_MODBUS_EVENT_CONFIG_PENDING 0x01UL

#if BSP_UART5_BAUD_RATE != APP_MODBUS_RTU_BAUD_RATE
#error "UART5 baud rate must match the Modbus RTU application setting"
#endif

static TX_THREAD modbus_task_control_block;
static TX_EVENT_FLAGS_GROUP modbus_control_events;
static uint64_t modbus_task_stack[APP_MODBUS_TASK_STACK_SIZE / sizeof(uint64_t)];

static ldc_easy_t modbus_queue;
static uint8_t modbus_ring[LDC_EASY_RING_BYTES(LD_MODBUS_RTU_MAX_ADU_LENGTH,
                                                APP_MODBUS_PACKET_COUNT)];
static ldc_packet_t modbus_packets[APP_MODBUS_PACKET_COUNT];
static uint8_t modbus_request[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t modbus_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static ld_modbus_ldc_rtu_server_t modbus_server;
static ld_modbus_server_map_t modbus_map;

static uint8_t modbus_coils[APP_MODBUS_COIL_COUNT];
static uint8_t modbus_discrete_inputs[APP_MODBUS_DISCRETE_COUNT];
static uint16_t modbus_holding_registers[APP_MODBUS_HOLDING_COUNT];
static uint16_t modbus_input_registers[APP_MODBUS_INPUT_COUNT];
static app_modbus_rtu_config_t modbus_active_config;
static app_modbus_rtu_config_t modbus_pending_config;
static volatile uint32_t modbus_received_bytes;
static uint8_t modbus_started;

static void app_modbus_task_entry(ULONG thread_input);
static void app_modbus_receive(const uint8_t *data,
                               uint16_t length,
                               void *argument);
static int app_modbus_send(void *user, const uint8_t *data, size_t length);
static uint32_t app_modbus_lock(void *user);
static void app_modbus_unlock(void *user, uint32_t state);
static void app_modbus_apply_pending_config(void);
static void app_modbus_apply_outputs(void);
static void app_modbus_update_inputs(void);

HAL_StatusTypeDef app_modbus_rtu_start(void)
{
    ldc_easy_config_t queue_config;
    app_device_config_t stored_config;
    uint8_t loaded_from_flash;
    UINT thread_status;

    if (modbus_started != 0U)
    {
        return HAL_BUSY;
    }

    memset(&queue_config, 0, sizeof(queue_config));
    memset(&modbus_map, 0, sizeof(modbus_map));
    memset(modbus_coils, 0, sizeof(modbus_coils));
    memset(modbus_discrete_inputs, 0, sizeof(modbus_discrete_inputs));
    memset(modbus_holding_registers, 0, sizeof(modbus_holding_registers));
    memset(modbus_input_registers, 0, sizeof(modbus_input_registers));
    memset(&modbus_active_config, 0, sizeof(modbus_active_config));
    memset(&modbus_pending_config, 0, sizeof(modbus_pending_config));
    modbus_received_bytes = 0UL;

    stored_config.modbus_unit_id = APP_MODBUS_RTU_UNIT_ID;
    loaded_from_flash = 0U;
    if (app_device_config_load(&stored_config, &loaded_from_flash) != HAL_OK)
    {
        stored_config.modbus_unit_id = APP_MODBUS_RTU_UNIT_ID;
    }
    modbus_active_config.unit_id = stored_config.modbus_unit_id;
    modbus_pending_config = modbus_active_config;

    queue_config.ring_buffer = modbus_ring;
    queue_config.ring_size = sizeof(modbus_ring);
    queue_config.packet_pool = modbus_packets;
    queue_config.packet_count = APP_MODBUS_PACKET_COUNT;
    queue_config.max_frame = LD_MODBUS_RTU_MAX_ADU_LENGTH;
    queue_config.timeout_us = APP_MODBUS_FRAME_TIMEOUT_US;
    queue_config.mode = LDC_MODE_PROTECT;
    queue_config.lock = app_modbus_lock;
    queue_config.unlock = app_modbus_unlock;

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

    /* Read-only identification registers exposed through function code 0x04. */
    modbus_input_registers[0] = 0x0750U;
    modbus_input_registers[1] = 0x0001U;

    if (ld_modbus_ldc_rtu_server_init(&modbus_server,
                                      &modbus_queue,
                                      modbus_active_config.unit_id,
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

    thread_status = tx_event_flags_create(&modbus_control_events,
                                          "modbus_control");
    if (thread_status != TX_SUCCESS)
    {
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }

    thread_status = tx_thread_create(&modbus_task_control_block,
                                     "modbus_rtu_task",
                                     app_modbus_task_entry,
                                     0UL,
                                     modbus_task_stack,
                                     sizeof(modbus_task_stack),
                                     APP_MODBUS_TASK_PRIORITY,
                                     APP_MODBUS_TASK_PRIORITY,
                                     TX_NO_TIME_SLICE,
                                     TX_DONT_START);
    if (thread_status != TX_SUCCESS)
    {
        (void)tx_event_flags_delete(&modbus_control_events);
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }

    modbus_started = 1U;
    if (tx_thread_resume(&modbus_task_control_block) != TX_SUCCESS)
    {
        modbus_started = 0U;
        (void)tx_thread_delete(&modbus_task_control_block);
        (void)tx_event_flags_delete(&modbus_control_events);
        (void)bsp_rs485_receive_stop();
        return HAL_ERROR;
    }
    return HAL_OK;
}

HAL_StatusTypeDef app_modbus_rtu_request_config(
    const app_modbus_rtu_config_t *config)
{
    uint32_t interrupt_state;

    if ((config == NULL) ||
        (config->unit_id == 0U) ||
        (config->unit_id > 247U) ||
        (config->red_led_on > 1U) ||
        (config->buzzer_on > 1U))
    {
        return HAL_ERROR;
    }
    if (modbus_started == 0U)
    {
        return HAL_BUSY;
    }

    interrupt_state = app_modbus_lock(NULL);
    modbus_pending_config = *config;
    app_modbus_unlock(NULL, interrupt_state);

    return (tx_event_flags_set(&modbus_control_events,
                               APP_MODBUS_EVENT_CONFIG_PENDING,
                               TX_OR) == TX_SUCCESS) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef app_modbus_rtu_get_config(app_modbus_rtu_config_t *config)
{
    uint32_t interrupt_state;

    if ((config == NULL) || (modbus_started == 0U))
    {
        return HAL_ERROR;
    }

    interrupt_state = app_modbus_lock(NULL);
    *config = modbus_active_config;
    app_modbus_unlock(NULL, interrupt_state);
    return HAL_OK;
}

HAL_StatusTypeDef app_modbus_rtu_get_diagnostics(
    app_modbus_rtu_diagnostics_t *diagnostics)
{
    ld_modbus_ldc_diagnostics_t server_diagnostics;
    ldc_stats_t ldc_stats;

    if ((diagnostics == NULL) || (modbus_started == 0U))
    {
        return HAL_ERROR;
    }
    if (ld_modbus_ldc_rtu_server_get_diagnostics(&modbus_server,
                                                  &server_diagnostics) !=
        LD_MODBUS_STATUS_OK)
    {
        return HAL_ERROR;
    }
    if (!ldc_easy_get_stats(&modbus_queue, &ldc_stats))
    {
        return HAL_ERROR;
    }

    diagnostics->received_bytes = modbus_received_bytes;
    diagnostics->received_frames = server_diagnostics.received_frames;
    diagnostics->replied_frames = server_diagnostics.replied_frames;
    diagnostics->crc_errors = server_diagnostics.crc_errors;
    diagnostics->malformed_frames = server_diagnostics.malformed_frames;
    diagnostics->ldc_overflow = ldc_stats.overflow;
    diagnostics->ldc_drop = ldc_stats.drop;
    return HAL_OK;
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
        uint8_t did_work;

        if (tx_event_flags_get(&modbus_control_events,
                               APP_MODBUS_EVENT_CONFIG_PENDING,
                               TX_OR_CLEAR,
                               &actual_flags,
                               TX_NO_WAIT) == TX_SUCCESS)
        {
            app_modbus_apply_pending_config();
        }

        if (elapsed_us != 0U)
        {
            last_cycles += elapsed_us * cycles_per_us;
            ldc_easy_tick_us(&modbus_queue, elapsed_us);
        }

        do
        {
            did_work = 0U;
            (void)ld_modbus_ldc_rtu_server_poll(&modbus_server, &did_work);
            if (did_work != 0U)
            {
                app_modbus_apply_outputs();
                app_modbus_update_inputs();
            }
        } while (did_work != 0U);

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

static uint32_t app_modbus_lock(void *user)
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

static void app_modbus_unlock(void *user, uint32_t interrupt_state)
{
    (void)user;
    if ((__get_IPSR() == 0U) && (interrupt_state == 0U))
    {
        __DMB();
        __enable_irq();
    }
}

static void app_modbus_apply_pending_config(void)
{
    app_modbus_rtu_config_t config;
    uint32_t interrupt_state = app_modbus_lock(NULL);

    config = modbus_pending_config;
    modbus_active_config.unit_id = config.unit_id;
    app_modbus_unlock(NULL, interrupt_state);

    modbus_server.unit_id = config.unit_id;
    modbus_coils[0] = config.red_led_on;
    modbus_coils[1] = config.buzzer_on;
    app_modbus_apply_outputs();
}

static void app_modbus_apply_outputs(void)
{
    uint32_t interrupt_state;

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

    interrupt_state = app_modbus_lock(NULL);
    modbus_active_config.red_led_on = (modbus_coils[0] != 0U) ? 1U : 0U;
    modbus_active_config.buzzer_on = (modbus_coils[1] != 0U) ? 1U : 0U;
    app_modbus_unlock(NULL, interrupt_state);
}

static void app_modbus_update_inputs(void)
{
    ld_modbus_ldc_diagnostics_t diagnostics;
    ldc_stats_t ldc_stats;

    if (ld_modbus_ldc_rtu_server_get_diagnostics(&modbus_server,
                                                  &diagnostics) == LD_MODBUS_STATUS_OK)
    {
        modbus_input_registers[2] = (uint16_t)diagnostics.received_frames;
        modbus_input_registers[3] = (uint16_t)diagnostics.replied_frames;
        modbus_input_registers[4] = (uint16_t)diagnostics.crc_errors;
        modbus_input_registers[5] = (uint16_t)diagnostics.malformed_frames;
    }

    if (ldc_easy_get_stats(&modbus_queue, &ldc_stats))
    {
        modbus_input_registers[6] = (uint16_t)ldc_stats.overflow;
        modbus_input_registers[7] = (uint16_t)ldc_stats.drop;
    }
}
