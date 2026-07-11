/**
 * @file test_ld_modbus.c
 * @brief Host regression for codecs, client helpers, and static server maps.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ld_modbus.h"
#include "ld_modbus_client.h"
#include "ld_modbus_server.h"

static void test_codecs(void)
{
    static const uint8_t request_body[] = {1U, 3U, 0U, 0U, 0U, 2U};
    static const uint8_t pdu[] = {3U, 0U, 0U, 0U, 2U};
    uint8_t adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    ld_modbus_adu_view_t view;
    size_t length;

    assert(ld_modbus_crc16(request_body, sizeof(request_body)) == 0x0BC4U);
    assert(ld_modbus_rtu_encode(1U, pdu, sizeof(pdu), adu, sizeof(adu), &length) ==
           LD_MODBUS_STATUS_OK);
    assert(length == 8U && adu[6] == 0xC4U && adu[7] == 0x0BU);
    assert(ld_modbus_rtu_decode(adu, length, &view) == LD_MODBUS_STATUS_OK);
    assert(view.unit_id == 1U && view.pdu_length == sizeof(pdu));
    adu[length - 1U] ^= 1U;
    assert(ld_modbus_rtu_decode(adu, length, &view) == LD_MODBUS_STATUS_BAD_CRC);

    assert(ld_modbus_tcp_encode(0x1234U, 7U, pdu, sizeof(pdu),
                                adu, sizeof(adu), &length) == LD_MODBUS_STATUS_OK);
    assert(length == 12U);
    assert(ld_modbus_tcp_decode(adu, length, &view) == LD_MODBUS_STATUS_OK);
    assert(view.transaction_id == 0x1234U && view.unit_id == 7U);
    adu[3] = 1U;
    assert(ld_modbus_tcp_decode(adu, length, &view) ==
           LD_MODBUS_STATUS_BAD_PROTOCOL_ID);
}

static void test_client_and_server(void)
{
    uint8_t coils[16] = {0};
    uint8_t discrete[16] = {0};
    uint16_t holding[16] = {0x4C44U, 1U};
    uint16_t input[16] = {0x4835U, 0x6301U};
    const ld_modbus_server_map_t map =
    {
        coils, 0U, 16U,
        discrete, 0U, 16U,
        holding, 0U, 16U,
        input, 0U, 16U
    };
    uint8_t request_pdu[LD_MODBUS_MAX_PDU_LENGTH];
    uint8_t request_adu[LD_MODBUS_RTU_MAX_ADU_LENGTH];
    uint8_t response_adu[LD_MODBUS_RTU_MAX_ADU_LENGTH];
    uint16_t values[2];
    uint8_t exception_code;
    ld_modbus_adu_view_t view;
    ld_modbus_server_action_t action;
    size_t pdu_length;
    size_t request_length;
    size_t response_length;

    assert(ld_modbus_client_build_read_request(
               LD_MODBUS_FC_READ_HOLDING_REGISTERS, 0U, 2U,
               request_pdu, sizeof(request_pdu), &pdu_length) == LD_MODBUS_STATUS_OK);
    assert(ld_modbus_rtu_encode(1U, request_pdu, pdu_length,
                                request_adu, sizeof(request_adu), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_rtu_adu(
               &map, 1U, request_adu, request_length,
               response_adu, sizeof(response_adu), &response_length, &action) ==
           LD_MODBUS_STATUS_OK);
    assert(action == LD_MODBUS_SERVER_ACTION_REPLY);
    assert(ld_modbus_rtu_decode(response_adu, response_length, &view) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_client_parse_read_registers_response(
               LD_MODBUS_FC_READ_HOLDING_REGISTERS, 2U,
               view.pdu, view.pdu_length, values, 2U, &exception_code) ==
           LD_MODBUS_STATUS_OK);
    assert(values[0] == 0x4C44U && values[1] == 1U);

    assert(ld_modbus_client_build_write_single_register(
               5U, 0x55AAU, request_pdu, sizeof(request_pdu), &pdu_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_rtu_encode(0U, request_pdu, pdu_length,
                                request_adu, sizeof(request_adu), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_rtu_adu(
               &map, 1U, request_adu, request_length,
               response_adu, sizeof(response_adu), &response_length, &action) ==
           LD_MODBUS_STATUS_OK);
    assert(action == LD_MODBUS_SERVER_ACTION_BROADCAST_APPLIED);
    assert(response_length == 0U && holding[5] == 0x55AAU);

    request_pdu[0] = 0x7FU;
    assert(ld_modbus_server_process_pdu(&map, request_pdu, 1U,
                                        response_adu, sizeof(response_adu),
                                        &response_length) == LD_MODBUS_STATUS_OK);
    assert(response_length == 2U && response_adu[0] == 0xFFU &&
           response_adu[1] == LD_MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
}

static void test_all_supported_functions(void)
{
    uint8_t coils[16] = {0U};
    uint8_t discrete[16] = {1U, 0U, 1U, 1U};
    uint16_t holding[16] = {0U};
    uint16_t input[16] = {0x1111U, 0x2222U};
    const ld_modbus_server_map_t map =
    {
        coils, 0U, 16U,
        discrete, 0U, 16U,
        holding, 0U, 16U,
        input, 0U, 16U
    };
    uint8_t request[LD_MODBUS_MAX_PDU_LENGTH];
    uint8_t response[LD_MODBUS_MAX_PDU_LENGTH];
    uint8_t bit_values[4];
    uint16_t register_values[4];
    const uint8_t coil_writes[4] = {1U, 0U, 1U, 1U};
    const uint16_t register_writes[2] = {0x1234U, 0xABCDU};
    uint8_t exception_code = 0U;
    size_t request_length;
    size_t response_length;

    assert(ld_modbus_client_build_read_request(
               LD_MODBUS_FC_READ_COILS, 0U, 4U, request, sizeof(request),
               &request_length) == LD_MODBUS_STATUS_OK);
    coils[0] = 1U;
    coils[2] = 1U;
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_client_parse_read_bits_response(
               LD_MODBUS_FC_READ_COILS, 4U, response, response_length,
               bit_values, 4U, &exception_code) == LD_MODBUS_STATUS_OK);
    assert(bit_values[0] == 1U && bit_values[1] == 0U && bit_values[2] == 1U);

    assert(ld_modbus_client_build_read_request(
               LD_MODBUS_FC_READ_DISCRETE_INPUTS, 0U, 4U, request,
               sizeof(request), &request_length) == LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_client_parse_read_bits_response(
               LD_MODBUS_FC_READ_DISCRETE_INPUTS, 4U, response, response_length,
               bit_values, 4U, &exception_code) == LD_MODBUS_STATUS_OK);
    assert(bit_values[0] == 1U && bit_values[2] == 1U && bit_values[3] == 1U);

    assert(ld_modbus_client_build_read_request(
               LD_MODBUS_FC_READ_INPUT_REGISTERS, 0U, 2U, request,
               sizeof(request), &request_length) == LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_client_parse_read_registers_response(
               LD_MODBUS_FC_READ_INPUT_REGISTERS, 2U, response, response_length,
               register_values, 2U, &exception_code) == LD_MODBUS_STATUS_OK);
    assert(register_values[0] == 0x1111U && register_values[1] == 0x2222U);

    assert(ld_modbus_client_build_write_single_coil(
               1U, 1U, request, sizeof(request), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(coils[1] == 1U);
    assert(ld_modbus_client_parse_write_response(
               LD_MODBUS_FC_WRITE_SINGLE_COIL, 1U, 0xFF00U,
               response, response_length, &exception_code) == LD_MODBUS_STATUS_OK);

    assert(ld_modbus_client_build_write_single_register(
               3U, 0x55AAU, request, sizeof(request), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(holding[3] == 0x55AAU);
    assert(ld_modbus_client_parse_write_response(
               LD_MODBUS_FC_WRITE_SINGLE_REGISTER, 3U, 0x55AAU,
               response, response_length, &exception_code) == LD_MODBUS_STATUS_OK);

    assert(ld_modbus_client_build_write_multiple_coils(
               4U, coil_writes, 4U, request, sizeof(request), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(memcmp(&coils[4], coil_writes, sizeof(coil_writes)) == 0);
    assert(ld_modbus_client_parse_write_response(
               LD_MODBUS_FC_WRITE_MULTIPLE_COILS, 4U, 4U,
               response, response_length, &exception_code) == LD_MODBUS_STATUS_OK);

    assert(ld_modbus_client_build_write_multiple_registers(
               6U, register_writes, 2U, request, sizeof(request), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(holding[6] == 0x1234U && holding[7] == 0xABCDU);
    assert(ld_modbus_client_parse_write_response(
               LD_MODBUS_FC_WRITE_MULTIPLE_REGISTERS, 6U, 2U,
               response, response_length, &exception_code) == LD_MODBUS_STATUS_OK);

    holding[8] = 0xAAAAU;
    assert(ld_modbus_client_build_mask_write_register(
               8U, 0x0F0FU, 0x0050U, request, sizeof(request), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(holding[8] == 0x0A5AU);
    assert(ld_modbus_client_parse_mask_write_response(
               8U, 0x0F0FU, 0x0050U, response, response_length,
               &exception_code) == LD_MODBUS_STATUS_OK);

    assert(ld_modbus_client_build_write_read_multiple_registers(
               9U, 2U, 9U, register_writes, 2U, request, sizeof(request),
               &request_length) == LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_pdu(&map, request, request_length, response,
                                        sizeof(response), &response_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_client_parse_read_registers_response(
               LD_MODBUS_FC_WRITE_READ_MULTIPLE_REGISTERS, 2U,
               response, response_length, register_values, 2U,
               &exception_code) == LD_MODBUS_STATUS_OK);
    assert(register_values[0] == 0x1234U && register_values[1] == 0xABCDU);
}

static void test_errors_and_tcp_server(void)
{
    uint8_t coils[2] = {0U};
    uint8_t discrete[2] = {0U};
    uint16_t holding[2] = {0U};
    uint16_t input[2] = {0U};
    const ld_modbus_server_map_t map =
    {
        coils, 0U, 2U, discrete, 0U, 2U,
        holding, 0U, 2U, input, 0U, 2U
    };
    uint8_t request_pdu[] = {LD_MODBUS_FC_READ_HOLDING_REGISTERS, 0U, 2U, 0U, 1U};
    uint8_t request_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    uint8_t response_adu[LD_MODBUS_TCP_MAX_ADU_LENGTH];
    uint8_t response_pdu[8];
    ld_modbus_adu_view_t view;
    size_t request_length;
    size_t response_length;

    assert(ld_modbus_server_process_pdu(&map, request_pdu, sizeof(request_pdu),
                                        response_pdu, sizeof(response_pdu),
                                        &response_length) == LD_MODBUS_STATUS_OK);
    assert(response_pdu[0] == (LD_MODBUS_FC_READ_HOLDING_REGISTERS | 0x80U));
    assert(response_pdu[1] == LD_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);

    request_pdu[1] = 0U;
    request_pdu[2] = 0U;
    request_pdu[3] = 0U;
    request_pdu[4] = 0U;
    assert(ld_modbus_server_process_pdu(&map, request_pdu, sizeof(request_pdu),
                                        response_pdu, sizeof(response_pdu),
                                        &response_length) == LD_MODBUS_STATUS_OK);
    assert(response_pdu[1] == LD_MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
    assert(ld_modbus_server_process_pdu(&map, request_pdu, 4U,
                                        response_pdu, sizeof(response_pdu),
                                        &response_length) == LD_MODBUS_STATUS_MALFORMED_FRAME);

    request_pdu[3] = 0U;
    request_pdu[4] = 1U;
    assert(ld_modbus_tcp_encode(0x3344U, 7U, request_pdu, sizeof(request_pdu),
                                request_adu, sizeof(request_adu), &request_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_server_process_tcp_adu(
               &map, request_adu, request_length, response_adu,
               sizeof(response_adu), &response_length) == LD_MODBUS_STATUS_OK);
    assert(ld_modbus_tcp_decode(response_adu, response_length, &view) ==
           LD_MODBUS_STATUS_OK);
    assert(view.transaction_id == 0x3344U && view.unit_id == 7U);
    assert(view.pdu[0] == LD_MODBUS_FC_READ_HOLDING_REGISTERS);
}

int main(void)
{
    test_codecs();
    test_client_and_server();
    test_all_supported_functions();
    test_errors_and_tcp_server();
    puts("ld_modbus host tests: ok");
    return 0;
}
