/**
 * @file modbus_slave_example.c
 * @brief Static-memory USART3 Modbus RTU slave using strict T1.5/T3.5 framing.
 */

#include "modbus_slave_example.h"

#include <string.h>

#include "ld_modbus.h"
#include "ld_modbus_rtu_framer.h"
#include "ld_modbus_server.h"
#include "modbus_app_config.h"
#include "modbus_port.h"

#define MODBUS_SLAVE_HOLDING_COUNT (64U)
#define MODBUS_SLAVE_INPUT_COUNT   (16U)
#define MODBUS_SLAVE_COIL_COUNT    (64U)
#define MODBUS_SLAVE_DISCRETE_COUNT (16U)

volatile modbus_slave_report_t g_modbus_slave_report;

static uint8_t g_active_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_ready_frame[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_request[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t g_coils[MODBUS_SLAVE_COIL_COUNT];
static uint8_t g_discrete_inputs[MODBUS_SLAVE_DISCRETE_COUNT];
static uint16_t g_holding_registers[MODBUS_SLAVE_HOLDING_COUNT];
static uint16_t g_input_registers[MODBUS_SLAVE_INPUT_COUNT];
static ld_modbus_rtu_framer_t g_framer;
static ld_modbus_server_map_t g_map;

/** @brief Publish diagnostics through protocol-read-only input registers 0..7. */
static void modbus_slave_update_diagnostics(void)
{
    (void)ld_modbus_server_map_set_input_register(&g_map, 0U, 0xF767U);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 1U, (uint16_t)g_modbus_slave_report.rx_frames);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 2U, (uint16_t)g_modbus_slave_report.tx_frames);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 3U, (uint16_t)g_modbus_slave_report.crc_errors);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 4U, (uint16_t)g_modbus_slave_report.protocol_errors);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 5U, (uint16_t)g_modbus_slave_report.t15_violations);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 6U, (uint16_t)g_modbus_slave_report.t15_us);
    (void)ld_modbus_server_map_set_input_register(
        &g_map, 7U, (uint16_t)g_modbus_slave_report.t35_us);
}

/** @brief Bind application-owned static tables to the ld_modbus server map. */
static void modbus_slave_init_map(void)
{
    memset(&g_map, 0, sizeof(g_map));
    g_map.coils = g_coils;
    g_map.coils_count = MODBUS_SLAVE_COIL_COUNT;
    g_map.discrete_inputs = g_discrete_inputs;
    g_map.discrete_inputs_count = MODBUS_SLAVE_DISCRETE_COUNT;
    g_map.holding_registers = g_holding_registers;
    g_map.holding_registers_count = MODBUS_SLAVE_HOLDING_COUNT;
    g_map.input_registers = g_input_registers;
    g_map.input_registers_count = MODBUS_SLAVE_INPUT_COUNT;
}

/** @brief Process and optionally answer one complete RTU request. */
static void modbus_slave_process_frame(const uint8_t *request,
                                       uint16_t request_length)
{
    ld_modbus_server_action_t action = LD_MODBUS_SERVER_ACTION_IGNORED;
    ld_modbus_status_t status;
    size_t response_length = 0U;

    g_modbus_slave_report.rx_frames++;
    modbus_slave_update_diagnostics();

    status = ld_modbus_server_process_rtu_adu(&g_map,
                                               MODBUS_APP_UNIT_ID,
                                               request,
                                               request_length,
                                               g_response,
                                               sizeof(g_response),
                                               &response_length,
                                               &action);
    if(status != LD_MODBUS_STATUS_OK)
    {
        if(status == LD_MODBUS_STATUS_BAD_CRC)
        {
            g_modbus_slave_report.crc_errors++;
        }
        else
        {
            g_modbus_slave_report.protocol_errors++;
        }
        modbus_slave_update_diagnostics();
        return;
    }

    if((action == LD_MODBUS_SERVER_ACTION_REPLY) && (response_length > 0U))
    {
        if(modbus_port_write(g_response,
                             (uint16_t)response_length,
                             MODBUS_APP_TX_TIMEOUT_MS) == HAL_OK)
        {
            g_modbus_slave_report.tx_frames++;
        }
        else
        {
            g_modbus_slave_report.protocol_errors++;
        }
    }

    modbus_slave_update_diagnostics();
}

/** @brief Initialize the slave example after CubeMX has initialized USART3. */
HAL_StatusTypeDef modbus_slave_example_init(void)
{
    memset((void *)&g_modbus_slave_report, 0, sizeof(g_modbus_slave_report));
    memset(g_coils, 0, sizeof(g_coils));
    memset(g_discrete_inputs, 0, sizeof(g_discrete_inputs));
    memset(g_holding_registers, 0, sizeof(g_holding_registers));
    memset(g_input_registers, 0, sizeof(g_input_registers));

    modbus_slave_init_map();
    if(!ld_modbus_rtu_framer_init(&g_framer,
                                  g_active_frame,
                                  g_ready_frame,
                                  sizeof(g_active_frame),
                                  MODBUS_APP_BAUD_RATE,
                                  MODBUS_APP_BITS_PER_CHAR))
    {
        return HAL_ERROR;
    }

    g_modbus_slave_report.t15_us = g_framer.t15_us;
    g_modbus_slave_report.t35_us = g_framer.t35_us;
    modbus_slave_update_diagnostics();
    return modbus_port_init(MODBUS_APP_BAUD_RATE);
}

/** @brief Run bounded receive, framing, and request processing work. */
void modbus_slave_example_poll(void)
{
    uint8_t byte;
    uint32_t timestamp_us;
    uint16_t request_length;

    while(modbus_port_try_read(&byte, &timestamp_us))
    {
        ld_modbus_rtu_framer_on_byte(&g_framer, byte, timestamp_us);
    }

    ld_modbus_rtu_framer_poll(&g_framer, modbus_port_time_us());
    while(ld_modbus_rtu_framer_take(&g_framer,
                                    g_request,
                                    sizeof(g_request),
                                    &request_length))
    {
        modbus_slave_process_frame(g_request, request_length);
    }

    g_modbus_slave_report.t15_violations = g_framer.diag.t15_violations;
    modbus_slave_update_diagnostics();
}
