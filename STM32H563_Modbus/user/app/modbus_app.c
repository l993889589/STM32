/**
 * @file modbus_app.c
 * @brief Static-memory Modbus RTU slave bound to board RS485-1 through LDC.
 */

#include "modbus_app.h"

#include <stddef.h>
#include <string.h>

#include "bsp_uart.h"
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
                   BSP_UART_RX_MODE_DMA : BSP_UART_RX_MODE_IT
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

    for(poll_index = 0U; poll_index < MODBUS_APP_MAX_POLLS_PER_STEP; ++poll_index)
    {
        protocol_status = ld_modbus_ldc_rtu_server_poll(&modbus_app_server,
                                                        &did_work);
        status = modbus_app_protocol_status(protocol_status);
        if(status != BSP_STATUS_OK || did_work == 0U)
        {
            return status;
        }
    }
    return BSP_STATUS_OK;
}
