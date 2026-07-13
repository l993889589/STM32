/**
 * @file test_ld_modbus_ldc.c
 * @brief Host integration tests for LDC-framed Modbus RTU server traffic.
 */

#include "ld_modbus_client.h"
#include "ld_modbus_ldc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_PACKET_COUNT 4U

static ldc_easy_t test_queue;
static uint8_t test_ring[LDC_EASY_RING_BYTES(LD_MODBUS_RTU_MAX_ADU_LENGTH,
                                             TEST_PACKET_COUNT)];
static ldc_packet_t test_packets[TEST_PACKET_COUNT];
static uint8_t test_request[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t test_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static uint8_t captured_response[LD_MODBUS_RTU_MAX_ADU_LENGTH];
static size_t captured_length;

/** @brief Capture one adapter response without a platform UART dependency. */
static int test_send(void *user, const uint8_t *data, size_t length)
{
    (void)user;
    assert(length <= sizeof(captured_response));
    memcpy(captured_response, data, length);
    captured_length = length;
    return (int)length;
}

/** @brief Push one complete RTU ADU through the public LDC idle-frame API. */
static void test_push_frame(const uint8_t *frame, size_t length)
{
    assert(ldc_easy_rx_idle(&test_queue, frame, (uint32_t)length) == length);
}

/** @brief Run the LDC integration regression suite. */
int main(void)
{
    ldc_easy_config_t queue_config;
    ld_modbus_server_map_t map;
    ld_modbus_ldc_rtu_server_t server;
    ld_modbus_ldc_diagnostics_t diagnostics;
    uint16_t holding[16];
    uint16_t parsed[2];
    uint8_t pdu[LD_MODBUS_MAX_PDU_LENGTH];
    uint8_t adu[LD_MODBUS_RTU_MAX_ADU_LENGTH];
    ld_modbus_adu_view_t view;
    ld_modbus_status_t parse_status;
    size_t pdu_length;
    size_t adu_length;
    uint8_t did_work;
    uint16_t index;

    memset(&queue_config, 0, sizeof(queue_config));
    queue_config.ring_buffer = test_ring;
    queue_config.ring_size = sizeof(test_ring);
    queue_config.packet_pool = test_packets;
    queue_config.packet_count = TEST_PACKET_COUNT;
    queue_config.max_frame = LD_MODBUS_RTU_MAX_ADU_LENGTH;
    queue_config.mode = LDC_MODE_PROTECT;
    assert(ldc_easy_init(&test_queue, &queue_config));

    memset(&map, 0, sizeof(map));
    for(index = 0U; index < 16U; ++index)
        holding[index] = (uint16_t)(0x1000U + index);
    map.holding_registers = holding;
    map.holding_registers_count = 16U;

    assert(ld_modbus_ldc_rtu_server_init(&server, &test_queue, 1U, &map,
                                         test_send, NULL,
                                         test_request, sizeof(test_request),
                                         test_response, sizeof(test_response)) ==
           LD_MODBUS_STATUS_OK);

    assert(ld_modbus_client_build_read_request(LD_MODBUS_FC_READ_HOLDING_REGISTERS,
                                               2U, 2U, pdu, sizeof(pdu), &pdu_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_rtu_encode(1U, pdu, pdu_length, adu, sizeof(adu), &adu_length) ==
           LD_MODBUS_STATUS_OK);
    test_push_frame(adu, adu_length);
    assert(ld_modbus_ldc_rtu_server_poll(&server, &did_work) == LD_MODBUS_STATUS_OK);
    assert(did_work == 1U && captured_length > 0U);
    assert(ld_modbus_rtu_decode(captured_response, captured_length, &view) ==
           LD_MODBUS_STATUS_OK);
    parse_status = ld_modbus_client_parse_read_registers_response(
        LD_MODBUS_FC_READ_HOLDING_REGISTERS, 2U,
        view.pdu, view.pdu_length, parsed, 2U, NULL);
    if(parse_status != LD_MODBUS_STATUS_OK)
    {
        size_t byte_index;
        fprintf(stderr, "parse failed: %s, ADU:", ld_modbus_status_string(parse_status));
        for(byte_index = 0U; byte_index < captured_length; ++byte_index)
            fprintf(stderr, " %02X", captured_response[byte_index]);
        fprintf(stderr, "\n");
    }
    assert(parse_status == LD_MODBUS_STATUS_OK);
    assert(parsed[0] == 0x1002U && parsed[1] == 0x1003U);

    /* A valid broadcast write changes data and deliberately sends no reply. */
    captured_length = 0U;
    assert(ld_modbus_client_build_write_single_register(4U, 0xBEEFU,
                                                        pdu, sizeof(pdu), &pdu_length) ==
           LD_MODBUS_STATUS_OK);
    assert(ld_modbus_rtu_encode(0U, pdu, pdu_length, adu, sizeof(adu), &adu_length) ==
           LD_MODBUS_STATUS_OK);
    test_push_frame(adu, adu_length);
    assert(ld_modbus_ldc_rtu_server_poll(&server, &did_work) == LD_MODBUS_STATUS_OK);
    assert(did_work == 1U && holding[4] == 0xBEEFU && captured_length == 0U);

    adu[adu_length - 1U] ^= 1U;
    test_push_frame(adu, adu_length);
    assert(ld_modbus_ldc_rtu_server_poll(&server, &did_work) == LD_MODBUS_STATUS_BAD_CRC);

    assert(ld_modbus_ldc_rtu_server_get_diagnostics(&server, &diagnostics) ==
           LD_MODBUS_STATUS_OK);
    assert(diagnostics.received_frames == 3U);
    assert(diagnostics.replied_frames == 1U);
    assert(diagnostics.broadcast_frames == 1U);
    assert(diagnostics.crc_errors == 1U);

    puts("ld_modbus LDC integration tests passed");
    return 0;
}
