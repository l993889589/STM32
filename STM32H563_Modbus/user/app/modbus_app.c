/**
 * @file modbus_app.c
 * @brief Static-memory Modbus RTU slave bound to board RS485-1 through LDC.
 */

#include "modbus_app.h"

#include <stddef.h>
#include <string.h>

#include "bsp_uart.h"
#include "ld_modbus_client.h"
#include "ld_modbus_ldc.h"
#include "transport_uart_ldc.h"
#include "target_config.h"

#define MODBUS_APP_UNIT_ID             (1U)
#define MODBUS_APP_BAUD_RATE           (115200U)
#define MODBUS_APP_HIGH_BAUD_THRESHOLD (19200U)
#define MODBUS_APP_HIGH_BAUD_T3_5_US   (1750U)
#define MODBUS_APP_T3_5_X10            (35U)
#define MODBUS_APP_PACKET_COUNT        (4U)
#define MODBUS_APP_REGISTER_COUNT      (64U)
#define MODBUS_APP_BIT_COUNT           (64U)
#define MODBUS_APP_MAX_POLLS_PER_STEP  (4U)
#define MODBUS_APP_TX_TIMEOUT_MS       (20U)
#define MODBUS_APP_MASTER_COMMAND_REG  (60U)
#define MODBUS_APP_MASTER_STATUS_REG   (61U)
#define MODBUS_APP_MASTER_STEP_REG     (62U)
#define MODBUS_APP_MASTER_ERROR_REG    (63U)
#define MODBUS_APP_MASTER_COMMAND      (0x4D53U)
#define MODBUS_APP_MASTER_STATUS_IDLE  (0x0000U)
#define MODBUS_APP_MASTER_STATUS_RUN   (0x0001U)
#define MODBUS_APP_MASTER_STATUS_PASS  (0x600DU)
#define MODBUS_APP_MASTER_STATUS_FAIL  (0xDEADU)
#define MODBUS_APP_MASTER_START_US     (500000U)
#define MODBUS_APP_MASTER_RESPONSE_US  (500000U)
#define MODBUS_APP_MASTER_RETURN_US    (250000U)

/** @brief Runtime role states used by the self-contained master/slave example. */
typedef enum
{
    MODBUS_APP_MODE_SLAVE = 0,
    MODBUS_APP_MODE_MASTER_START_DELAY,
    MODBUS_APP_MODE_MASTER_WAIT_IDENTITY,
    MODBUS_APP_MODE_MASTER_WAIT_WRITE,
    MODBUS_APP_MODE_MASTER_WAIT_READBACK,
    MODBUS_APP_MODE_MASTER_RETURN_DELAY
} modbus_app_mode_t;

static uint8_t modbus_app_coils[MODBUS_APP_BIT_COUNT];
static uint8_t modbus_app_discrete_inputs[MODBUS_APP_BIT_COUNT];
static uint16_t modbus_app_holding_registers[MODBUS_APP_REGISTER_COUNT];
static uint16_t modbus_app_input_registers[MODBUS_APP_REGISTER_COUNT];
static uint8_t modbus_app_ldc_ring[LDC_EASY_RING_BYTES(
    LD_MODBUS_RTU_MAX_ADU_LENGTH, MODBUS_APP_PACKET_COUNT)];
static ldc_packet_t modbus_app_ldc_packets[MODBUS_APP_PACKET_COUNT];
static uint8_t modbus_app_request[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t modbus_app_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static transport_uart_ldc_t modbus_app_transport;
static ld_modbus_ldc_rtu_server_t modbus_app_server;
static modbus_app_mode_t modbus_app_mode;
static uint32_t modbus_app_mode_elapsed_us;
static const ld_modbus_server_map_t modbus_app_map =
{
    .coils = modbus_app_coils,
    .coils_start = 0U,
    .coils_count = MODBUS_APP_BIT_COUNT,
    .discrete_inputs = modbus_app_discrete_inputs,
    .discrete_inputs_start = 0U,
    .discrete_inputs_count = MODBUS_APP_BIT_COUNT,
    .holding_registers = modbus_app_holding_registers,
    .holding_registers_start = 0U,
    .holding_registers_count = MODBUS_APP_REGISTER_COUNT,
    .input_registers = modbus_app_input_registers,
    .input_registers_start = 0U,
    .input_registers_count = MODBUS_APP_REGISTER_COUNT
};

/**
 * @brief Derive RTU T3.5 from the BSP-owned active UART line configuration.
 * @return Timeout in microseconds, or zero if the UART configuration is unavailable.
 */
static uint32_t modbus_app_rtu_t3_5_us(void)
{
    bsp_uart_config_t config;
    uint8_t parity_bits;

    if(bsp_uart_get_config(BOARD_UART_RS485_1, &config) != BSP_STATUS_OK)
    {
        return 0U;
    }
    if(config.baud_rate > MODBUS_APP_HIGH_BAUD_THRESHOLD)
    {
        return MODBUS_APP_HIGH_BAUD_T3_5_US;
    }
    parity_bits = config.parity == BSP_UART_PARITY_NONE ? 0U : 1U;
    return ldc_serial_silence_us(config.baud_rate,
                                 config.data_bits,
                                 parity_bits,
                                 config.stop_bits,
                                 MODBUS_APP_T3_5_X10);
}

/**
 * @brief Send one complete RTU response through the automatic-direction transceiver.
 */
static int modbus_app_send(void *user, const uint8_t *data, size_t length)
{
    bsp_status_t status;

    (void)user;
    if(length > UINT32_MAX)
    {
        return -1;
    }
    status = bsp_uart_write(BOARD_UART_RS485_1,
                            data,
                            (uint32_t)length,
                            MODBUS_APP_TX_TIMEOUT_MS);
    return status == BSP_STATUS_OK ? (int)length : -1;
}

/**
 * @brief Convert protocol adapter results into the application service status model.
 */
static bsp_status_t modbus_app_protocol_status(ld_modbus_status_t status)
{
    switch(status)
    {
        case LD_MODBUS_STATUS_OK:
        case LD_MODBUS_STATUS_BAD_CRC:
        case LD_MODBUS_STATUS_MALFORMED_FRAME:
        case LD_MODBUS_STATUS_BUFFER_TOO_SMALL:
            return BSP_STATUS_OK;
        default:
            return BSP_STATUS_IO_ERROR;
    }
}

/** @brief Remove stale complete frames before changing RTU ownership roles. */
static void modbus_app_drain_frames(void)
{
    while(ldc_easy_pop(&modbus_app_transport.ldc,
                       modbus_app_request,
                       sizeof(modbus_app_request)) > 0)
    {
    }
}

/** @brief Record a master-demo failure and schedule a return to slave mode. */
static void modbus_app_master_fail(ld_modbus_status_t error)
{
    modbus_app_holding_registers[MODBUS_APP_MASTER_STATUS_REG] =
        MODBUS_APP_MASTER_STATUS_FAIL;
    modbus_app_holding_registers[MODBUS_APP_MASTER_ERROR_REG] = (uint16_t)error;
    modbus_app_mode = MODBUS_APP_MODE_MASTER_RETURN_DELAY;
    modbus_app_mode_elapsed_us = 0U;
}

/** @brief Encode and transmit one caller-built PDU as an RTU master request. */
static bsp_status_t modbus_app_master_send_pdu(const uint8_t *pdu,
                                               size_t pdu_length)
{
    ld_modbus_status_t protocol_status;
    size_t adu_length;

    protocol_status = ld_modbus_rtu_encode(MODBUS_APP_UNIT_ID,
                                            pdu,
                                            pdu_length,
                                            modbus_app_request,
                                            sizeof(modbus_app_request),
                                            &adu_length);
    if(protocol_status != LD_MODBUS_STATUS_OK)
    {
        modbus_app_master_fail(protocol_status);
        return BSP_STATUS_IO_ERROR;
    }
    if(modbus_app_send(NULL, modbus_app_request, adu_length) != (int)adu_length)
    {
        modbus_app_master_fail(LD_MODBUS_STATUS_MALFORMED_FRAME);
        return BSP_STATUS_IO_ERROR;
    }
    modbus_app_mode_elapsed_us = 0U;
    return BSP_STATUS_OK;
}

/** @brief Send FC03 to prove that the remote slave identity can be parsed. */
static bsp_status_t modbus_app_master_send_identity(void)
{
    size_t pdu_length;
    ld_modbus_status_t status = ld_modbus_client_build_read_request(
        LD_MODBUS_FC_READ_HOLDING_REGISTERS, 0U, 2U,
        modbus_app_response, sizeof(modbus_app_response), &pdu_length);

    if(status != LD_MODBUS_STATUS_OK)
    {
        modbus_app_master_fail(status);
        return BSP_STATUS_IO_ERROR;
    }
    return modbus_app_master_send_pdu(modbus_app_response, pdu_length);
}

/** @brief Send FC06; the following readback is only issued after its echo validates. */
static bsp_status_t modbus_app_master_send_write(void)
{
    size_t pdu_length;
    ld_modbus_status_t status = ld_modbus_client_build_write_single_register(
        5U, 0x55AAU, modbus_app_response, sizeof(modbus_app_response), &pdu_length);

    if(status != LD_MODBUS_STATUS_OK)
    {
        modbus_app_master_fail(status);
        return BSP_STATUS_IO_ERROR;
    }
    return modbus_app_master_send_pdu(modbus_app_response, pdu_length);
}

/** @brief Send FC03 readback for the register written by the previous step. */
static bsp_status_t modbus_app_master_send_readback(void)
{
    size_t pdu_length;
    ld_modbus_status_t status = ld_modbus_client_build_read_request(
        LD_MODBUS_FC_READ_HOLDING_REGISTERS, 5U, 1U,
        modbus_app_response, sizeof(modbus_app_response), &pdu_length);

    if(status != LD_MODBUS_STATUS_OK)
    {
        modbus_app_master_fail(status);
        return BSP_STATUS_IO_ERROR;
    }
    return modbus_app_master_send_pdu(modbus_app_response, pdu_length);
}

/**
 * @brief Validate one silence-delimited master response and advance the demo.
 * @return BSP_STATUS_NOT_READY until LDC exposes a complete response frame.
 */
static bsp_status_t modbus_app_master_poll_response(void)
{
    ld_modbus_adu_view_t view;
    ld_modbus_status_t status;
    uint16_t values[2];
    uint8_t exception_code;
    int frame_length;

    frame_length = ldc_easy_pop(&modbus_app_transport.ldc,
                                modbus_app_request,
                                sizeof(modbus_app_request));
    if(frame_length == 0)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(frame_length < 0)
    {
        modbus_app_master_fail(LD_MODBUS_STATUS_BUFFER_TOO_SMALL);
        return BSP_STATUS_IO_ERROR;
    }
    status = ld_modbus_rtu_decode(modbus_app_request, (size_t)frame_length, &view);
    if(status != LD_MODBUS_STATUS_OK || view.unit_id != MODBUS_APP_UNIT_ID)
    {
        modbus_app_master_fail(status == LD_MODBUS_STATUS_OK ?
                               LD_MODBUS_STATUS_UNIT_MISMATCH : status);
        return BSP_STATUS_IO_ERROR;
    }

    if(modbus_app_mode == MODBUS_APP_MODE_MASTER_WAIT_IDENTITY)
    {
        status = ld_modbus_client_parse_read_registers_response(
            LD_MODBUS_FC_READ_HOLDING_REGISTERS, 2U,
            view.pdu, view.pdu_length, values, 2U, &exception_code);
        if(status != LD_MODBUS_STATUS_OK || values[0] != 0x4C44U || values[1] != 1U)
        {
            modbus_app_master_fail(status == LD_MODBUS_STATUS_OK ?
                                   LD_MODBUS_STATUS_TRANSACTION_MISMATCH : status);
            return BSP_STATUS_IO_ERROR;
        }
        modbus_app_holding_registers[MODBUS_APP_MASTER_STEP_REG] = 1U;
        if(modbus_app_master_send_write() != BSP_STATUS_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        modbus_app_mode = MODBUS_APP_MODE_MASTER_WAIT_WRITE;
        return BSP_STATUS_OK;
    }

    if(modbus_app_mode == MODBUS_APP_MODE_MASTER_WAIT_WRITE)
    {
        status = ld_modbus_client_parse_write_response(
            LD_MODBUS_FC_WRITE_SINGLE_REGISTER, 5U, 0x55AAU,
            view.pdu, view.pdu_length, &exception_code);
        if(status != LD_MODBUS_STATUS_OK)
        {
            modbus_app_master_fail(status);
            return BSP_STATUS_IO_ERROR;
        }
        modbus_app_holding_registers[MODBUS_APP_MASTER_STEP_REG] = 2U;
        if(modbus_app_master_send_readback() != BSP_STATUS_OK)
        {
            return BSP_STATUS_IO_ERROR;
        }
        modbus_app_mode = MODBUS_APP_MODE_MASTER_WAIT_READBACK;
        return BSP_STATUS_OK;
    }

    status = ld_modbus_client_parse_read_registers_response(
        LD_MODBUS_FC_READ_HOLDING_REGISTERS, 1U,
        view.pdu, view.pdu_length, values, 1U, &exception_code);
    if(status != LD_MODBUS_STATUS_OK || values[0] != 0x55AAU)
    {
        modbus_app_master_fail(status == LD_MODBUS_STATUS_OK ?
                               LD_MODBUS_STATUS_TRANSACTION_MISMATCH : status);
        return BSP_STATUS_IO_ERROR;
    }
    modbus_app_holding_registers[MODBUS_APP_MASTER_STEP_REG] = 3U;
    modbus_app_holding_registers[MODBUS_APP_MASTER_STATUS_REG] =
        MODBUS_APP_MASTER_STATUS_PASS;
    modbus_app_mode = MODBUS_APP_MODE_MASTER_RETURN_DELAY;
    modbus_app_mode_elapsed_us = 0U;
    return BSP_STATUS_OK;
}

/** @brief Advance the triggered board-master transaction without blocking. */
static bsp_status_t modbus_app_master_step(uint32_t elapsed_us)
{
    bsp_status_t status;

    modbus_app_mode_elapsed_us += elapsed_us;
    if(modbus_app_mode == MODBUS_APP_MODE_MASTER_START_DELAY)
    {
        if(modbus_app_mode_elapsed_us < MODBUS_APP_MASTER_START_US)
        {
            return BSP_STATUS_OK;
        }
        status = modbus_app_master_send_identity();
        if(status == BSP_STATUS_OK)
        {
            modbus_app_mode = MODBUS_APP_MODE_MASTER_WAIT_IDENTITY;
        }
        return status;
    }
    if(modbus_app_mode == MODBUS_APP_MODE_MASTER_RETURN_DELAY)
    {
        if(modbus_app_mode_elapsed_us >= MODBUS_APP_MASTER_RETURN_US)
        {
            modbus_app_drain_frames();
            modbus_app_mode = MODBUS_APP_MODE_SLAVE;
            modbus_app_mode_elapsed_us = 0U;
        }
        return BSP_STATUS_OK;
    }

    status = modbus_app_master_poll_response();
    if(status == BSP_STATUS_NOT_READY)
    {
        if(modbus_app_mode_elapsed_us >= MODBUS_APP_MASTER_RESPONSE_US)
        {
            modbus_app_master_fail(LD_MODBUS_STATUS_TRANSACTION_MISMATCH);
        }
        return BSP_STATUS_OK;
    }
    /* A failed transaction is observable in registers and is not fatal to service. */
    return status == BSP_STATUS_IO_ERROR ? BSP_STATUS_OK : status;
}

/**
 * @brief Initialize USART2, LDC framing, and the static Modbus register map.
 */
bsp_status_t modbus_app_init(void)
{
    bsp_uart_config_t uart_config =
    {
        .baud_rate = MODBUS_APP_BAUD_RATE,
        .receive_chunk_bytes = 64U,
        .data_bits = 8U,
        .parity = BSP_UART_PARITY_NONE,
        .stop_bits = 1U,
        .rx_mode = MODBUS_UART_RX_DMA != 0U ?
                   BSP_UART_RX_MODE_DMA : BSP_UART_RX_MODE_IT,
        .tx_mode = MODBUS_UART_TX_DMA != 0U ?
                   BSP_UART_TX_MODE_DMA : BSP_UART_TX_MODE_POLLING
    };
    transport_uart_ldc_config_t ldc_config =
    {
        .ring_buffer = modbus_app_ldc_ring,
        .ring_size = sizeof(modbus_app_ldc_ring),
        .packet_pool = modbus_app_ldc_packets,
        .packet_count = MODBUS_APP_PACKET_COUNT,
        .max_frame = LD_MODBUS_RTU_MAX_ADU_LENGTH,
        .timeout_us = 0U,
        .delimiter = 0U,
        .delimiter_enabled = 0U
    };
    bsp_status_t status;
    ld_modbus_status_t protocol_status;

    memset(modbus_app_coils, 0, sizeof(modbus_app_coils));
    memset(modbus_app_discrete_inputs, 0, sizeof(modbus_app_discrete_inputs));
    memset(modbus_app_holding_registers, 0, sizeof(modbus_app_holding_registers));
    memset(modbus_app_input_registers, 0, sizeof(modbus_app_input_registers));
    modbus_app_holding_registers[0] = 0x4C44U;
    modbus_app_holding_registers[1] =
        (uint16_t)((LD_MODBUS_VERSION_MAJOR << 8U) | LD_MODBUS_VERSION_MINOR);
    modbus_app_input_registers[0] = 0x4835U;
    modbus_app_input_registers[1] = 0x6301U;
    modbus_app_mode = MODBUS_APP_MODE_SLAVE;
    modbus_app_mode_elapsed_us = 0U;
    modbus_app_holding_registers[MODBUS_APP_MASTER_STATUS_REG] =
        MODBUS_APP_MASTER_STATUS_IDLE;

    status = bsp_uart_init(BOARD_UART_RS485_1, &uart_config);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    ldc_config.timeout_us = modbus_app_rtu_t3_5_us();
    if(ldc_config.timeout_us == 0U)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    status = transport_uart_ldc_init(&modbus_app_transport,
                                     BOARD_UART_RS485_1,
                                     &ldc_config);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    protocol_status = ld_modbus_ldc_rtu_server_init(
        &modbus_app_server,
        &modbus_app_transport.ldc,
        MODBUS_APP_UNIT_ID,
        &modbus_app_map,
        modbus_app_send,
        NULL,
        modbus_app_request,
        sizeof(modbus_app_request),
        modbus_app_response,
        sizeof(modbus_app_response));
    return modbus_app_protocol_status(protocol_status);
}

/**
 * @brief Advance receive framing and process a bounded number of RTU requests.
 */
bsp_status_t modbus_app_step(uint32_t elapsed_us)
{
    uint32_t poll_index;
    uint8_t did_work;
    bsp_status_t status;
    ld_modbus_status_t protocol_status;

    status = transport_uart_ldc_step(&modbus_app_transport, elapsed_us);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    if(modbus_app_mode != MODBUS_APP_MODE_SLAVE)
    {
        return modbus_app_master_step(elapsed_us);
    }

    for(poll_index = 0U; poll_index < MODBUS_APP_MAX_POLLS_PER_STEP; ++poll_index)
    {
        protocol_status = ld_modbus_ldc_rtu_server_poll(&modbus_app_server,
                                                        &did_work);
        status = modbus_app_protocol_status(protocol_status);
        if(status != BSP_STATUS_OK)
        {
            return status;
        }
        if(did_work == 0U)
        {
            break;
        }
    }
    if(modbus_app_holding_registers[MODBUS_APP_MASTER_COMMAND_REG] ==
       MODBUS_APP_MASTER_COMMAND)
    {
        modbus_app_holding_registers[MODBUS_APP_MASTER_COMMAND_REG] = 0U;
        modbus_app_holding_registers[MODBUS_APP_MASTER_STATUS_REG] =
            MODBUS_APP_MASTER_STATUS_RUN;
        modbus_app_holding_registers[MODBUS_APP_MASTER_STEP_REG] = 0U;
        modbus_app_holding_registers[MODBUS_APP_MASTER_ERROR_REG] = 0U;
        modbus_app_drain_frames();
        modbus_app_mode = MODBUS_APP_MODE_MASTER_START_DELAY;
        modbus_app_mode_elapsed_us = 0U;
    }
    return BSP_STATUS_OK;
}
