/**
 * @file modbus_network_app.c
 * @brief Credential-gated W800 Modbus TCP runtime entry for both roles.
 *
 * Storage is entirely static. The server owns a separate test map so an RTOS
 * network task never races the RS-485 service over mutable registers.
 */

#include "modbus_network_app.h"

#include <stdbool.h>
#include <string.h>

#include "ld_modbus_client.h"
#include "modbus_network_config.h"
#include "modbus_tcp_w800.h"

#define MODBUS_NETWORK_TABLE_COUNT       (64U)
#define MODBUS_NETWORK_CLIENT_PERIOD_MS  (1000U)
#define MODBUS_NETWORK_UNIT_ID           (1U)

#if MODBUS_W800_ENABLE
static modbus_tcp_w800_client_t modbus_network_client;
static modbus_tcp_w800_server_t modbus_network_server;
static uint8_t modbus_network_coils[MODBUS_NETWORK_TABLE_COUNT];
static uint8_t modbus_network_discrete_inputs[MODBUS_NETWORK_TABLE_COUNT];
static uint16_t modbus_network_holding[MODBUS_NETWORK_TABLE_COUNT];
static uint16_t modbus_network_input[MODBUS_NETWORK_TABLE_COUNT];
static uint8_t modbus_network_pdu[LD_MODBUS_MAX_PDU_LENGTH];
static uint32_t modbus_network_client_elapsed_ms;
static bool modbus_network_initialized;
static const ld_modbus_server_map_t modbus_network_map =
{
    .coils = modbus_network_coils,
    .coils_start = 0U,
    .coils_count = MODBUS_NETWORK_TABLE_COUNT,
    .discrete_inputs = modbus_network_discrete_inputs,
    .discrete_inputs_start = 0U,
    .discrete_inputs_count = MODBUS_NETWORK_TABLE_COUNT,
    .holding_registers = modbus_network_holding,
    .holding_registers_start = 0U,
    .holding_registers_count = MODBUS_NETWORK_TABLE_COUNT,
    .input_registers = modbus_network_input,
    .input_registers_start = 0U,
    .input_registers_count = MODBUS_NETWORK_TABLE_COUNT
};

/** @brief Build the transport configuration from credential-free/local macros. */
static transport_w800_tcp_config_t modbus_network_transport_config(void)
{
    const transport_w800_tcp_config_t config =
    {
        .ssid = MODBUS_W800_SSID,
        .password = MODBUS_W800_PASSWORD,
        .remote_host = MODBUS_W800_REMOTE_HOST,
        .remote_port = MODBUS_W800_REMOTE_PORT,
        .local_port = MODBUS_W800_LOCAL_PORT,
        .uart_baud_rate = MODBUS_W800_UART_BAUD_RATE,
        .role = MODBUS_W800_ROLE,
        .server_idle_timeout_s = MODBUS_W800_SERVER_IDLE_TIMEOUT_S
    };
    return config;
}
#endif

/** @brief Initialize the optional W800 service from task context. */
bsp_status_t modbus_network_app_init(void)
{
#if MODBUS_W800_ENABLE
    transport_w800_tcp_config_t config;

    if(modbus_network_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }
    if((MODBUS_W800_SSID[0] == '\0') || (MODBUS_W800_PASSWORD[0] == '\0'))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    memset(modbus_network_coils, 0, sizeof(modbus_network_coils));
    memset(modbus_network_discrete_inputs, 0, sizeof(modbus_network_discrete_inputs));
    memset(modbus_network_holding, 0, sizeof(modbus_network_holding));
    memset(modbus_network_input, 0, sizeof(modbus_network_input));
    modbus_network_holding[0] = 0x4C44U;
    modbus_network_holding[1] =
        (uint16_t)((LD_MODBUS_VERSION_MAJOR << 8U) | LD_MODBUS_VERSION_MINOR);
    modbus_network_input[0] = 0x4835U;
    modbus_network_input[1] = 0x6301U;
    config = modbus_network_transport_config();

    if(config.role == TRANSPORT_W800_TCP_SERVER)
    {
        const bsp_status_t status = modbus_tcp_w800_server_init(
            &modbus_network_server, &config, &modbus_network_map);
        modbus_network_initialized = status == BSP_STATUS_OK;
        return status;
    }
    else
    {
        const bsp_status_t status = modbus_tcp_w800_client_init(
            &modbus_network_client, &config, MODBUS_NETWORK_UNIT_ID);
        modbus_network_initialized = status == BSP_STATUS_OK;
        return status;
    }
#else
    return BSP_STATUS_OK;
#endif
}

/** @brief Poll the selected W800 role without allocating runtime memory. */
bsp_status_t modbus_network_app_step(uint32_t elapsed_ms)
{
#if MODBUS_W800_ENABLE
    if(!modbus_network_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(MODBUS_W800_ROLE == TRANSPORT_W800_TCP_SERVER)
    {
        bool did_work;
        return modbus_tcp_w800_server_step(&modbus_network_server, &did_work);
    }
    if(modbus_network_client.is_pending)
    {
        ld_modbus_adu_view_t response;
        uint16_t values[2];
        uint8_t exception_code;
        bsp_status_t status = modbus_tcp_w800_client_poll(
            &modbus_network_client, &response);

        if(status == BSP_STATUS_NOT_READY)
        {
            return BSP_STATUS_OK;
        }
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
        if(ld_modbus_client_parse_read_registers_response(
               LD_MODBUS_FC_READ_HOLDING_REGISTERS, 2U,
               response.pdu, response.pdu_length,
               values, 2U, &exception_code) != LD_MODBUS_STATUS_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        modbus_network_client_elapsed_ms = 0U;
        return BSP_STATUS_OK;
    }

    modbus_network_client_elapsed_ms += elapsed_ms;
    if(modbus_network_client_elapsed_ms >= MODBUS_NETWORK_CLIENT_PERIOD_MS)
    {
        size_t pdu_length;
        if(ld_modbus_client_build_read_request(
               LD_MODBUS_FC_READ_HOLDING_REGISTERS, 0U, 2U,
               modbus_network_pdu, sizeof(modbus_network_pdu), &pdu_length) !=
           LD_MODBUS_STATUS_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        return modbus_tcp_w800_client_begin(&modbus_network_client,
                                             modbus_network_pdu,
                                             pdu_length);
    }
#else
    (void)elapsed_ms;
#endif
    return BSP_STATUS_OK;
}

/** @brief Close the selected W800 role and permit a clean reconnect. */
bsp_status_t modbus_network_app_deinit(void)
{
#if MODBUS_W800_ENABLE
    bsp_status_t status;

    if(!modbus_network_initialized)
    {
        return BSP_STATUS_OK;
    }
    if(MODBUS_W800_ROLE == TRANSPORT_W800_TCP_SERVER)
    {
        status = transport_w800_tcp_close(&modbus_network_server.transport);
        memset(&modbus_network_server, 0, sizeof(modbus_network_server));
    }
    else
    {
        status = transport_w800_tcp_close(&modbus_network_client.transport);
        memset(&modbus_network_client, 0, sizeof(modbus_network_client));
    }
    modbus_network_client_elapsed_ms = 0U;
    modbus_network_initialized = false;
    return status;
#else
    return BSP_STATUS_OK;
#endif
}
