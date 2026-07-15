#include "ota_modbus_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_crc32(void)
{
    static const uint8_t text[] = "123456789";

    assert(ota_modbus_crc32(text, sizeof(text) - 1U) == 0xCBF43926UL);
}

static void test_hello_round_trip(void)
{
    ota_modbus_request_t request = {0};
    ota_modbus_request_t decoded_request;
    ota_modbus_response_t response = {0};
    ota_modbus_response_t decoded_response;
    uint8_t pdu[OTA_MODBUS_MAX_PDU_SIZE];
    size_t length;

    request.command = OTA_MODBUS_COMMAND_HELLO;
    request.session_id = 0x12345678UL;
    assert(ota_modbus_encode_request(&request, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(length == 7U);
    assert(ota_modbus_decode_request(pdu, length, &decoded_request) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded_request.command == request.command);
    assert(decoded_request.session_id == request.session_id);

    response.command = OTA_MODBUS_COMMAND_HELLO;
    response.status = OTA_MODBUS_STATUS_OK;
    response.session_id = request.session_id;
    response.capability_flags = OTA_MODBUS_CAP_BAUD_SWITCH |
                                OTA_MODBUS_CAP_RESUME |
                                OTA_MODBUS_CAP_SIGNED_IMAGE |
                                OTA_MODBUS_CAP_AB_SLOTS;
    response.max_data_size = OTA_MODBUS_MAX_DATA_SIZE;
    response.current_baud_rate = 115200U;
    response.baud_rate_count = 4U;
    response.baud_rates[0] = 115200U;
    response.baud_rates[1] = 230400U;
    response.baud_rates[2] = 460800U;
    response.baud_rates[3] = 921600U;
    assert(ota_modbus_encode_response(&response, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(ota_modbus_decode_response(pdu, length, &decoded_response) ==
           OTA_MODBUS_CODEC_OK);
    assert(memcmp(&response, &decoded_response, sizeof(response)) == 0);
}

static void test_baud_round_trip(void)
{
    ota_modbus_request_t request = {0};
    ota_modbus_request_t decoded;
    uint8_t pdu[OTA_MODBUS_MAX_PDU_SIZE];
    size_t length;

    request.command = OTA_MODBUS_COMMAND_SET_BAUD;
    request.session_id = 7U;
    request.baud_rate = 460800U;
    request.switch_delay_ms = 50U;
    request.fallback_timeout_ms = 2000U;
    assert(ota_modbus_encode_request(&request, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(ota_modbus_decode_request(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded.baud_rate == 460800U);
    assert(decoded.switch_delay_ms == 50U);
    assert(decoded.fallback_timeout_ms == 2000U);

    request.command = OTA_MODBUS_COMMAND_SYNC;
    assert(ota_modbus_encode_request(&request, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(ota_modbus_decode_request(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded.baud_rate == 460800U);
}

static void test_descriptor_round_trip(void)
{
    ota_modbus_request_t request = {0};
    ota_modbus_request_t decoded;
    uint8_t pdu[OTA_MODBUS_MAX_PDU_SIZE];
    size_t length;

    request.command = OTA_MODBUS_COMMAND_BEGIN;
    request.session_id = 0x89ABCDEFUL;
    request.descriptor.state = 2U;
    request.descriptor.image_version = 2026071501UL;
    request.descriptor.image_size = 861728U;
    request.descriptor.image_crc32 = 0xA5A55A5AUL;
    request.descriptor.image_flags = 2U;
    request.descriptor.load_address = 0x08020000UL;
    request.descriptor.entry_address = 0x08020101UL;
    for (uint32_t index = 0U; index < 32U; index++)
    {
        request.descriptor.image_sha256[index] = (uint8_t)index;
    }
    for (uint32_t index = 0U; index < 64U; index++)
    {
        request.descriptor.signature[index] = (uint8_t)(0x80U + index);
    }

    assert(ota_modbus_encode_request(&request, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(length == 131U);
    assert(ota_modbus_decode_request(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(memcmp(&request.descriptor,
                  &decoded.descriptor,
                  sizeof(request.descriptor)) == 0);
}

static void test_data_round_trip(void)
{
    ota_modbus_request_t request = {0};
    ota_modbus_request_t decoded;
    uint8_t data[OTA_MODBUS_MAX_DATA_SIZE];
    uint8_t pdu[OTA_MODBUS_MAX_PDU_SIZE];
    size_t length;

    for (uint32_t index = 0U; index < sizeof(data); index++)
    {
        data[index] = (uint8_t)(index ^ 0x5AU);
    }

    request.command = OTA_MODBUS_COMMAND_DATA;
    request.session_id = 11U;
    request.offset = 4096U;
    request.data = data;
    request.data_length = sizeof(data);
    assert(ota_modbus_encode_request(&request, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(length == 248U);
    assert(ota_modbus_decode_request(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded.offset == request.offset);
    assert(decoded.data_length == sizeof(data));
    assert(memcmp(decoded.data, data, sizeof(data)) == 0);
    assert(decoded.data_crc32 == ota_modbus_crc32(data, sizeof(data)));

    pdu[length - 1U] ^= 0x01U;
    assert(ota_modbus_decode_request(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded.data_crc32 != ota_modbus_crc32(decoded.data,
                                                  decoded.data_length));
}

static void test_status_and_errors(void)
{
    ota_modbus_response_t response = {0};
    ota_modbus_response_t decoded;
    uint8_t pdu[OTA_MODBUS_MAX_PDU_SIZE];
    size_t length;

    response.command = OTA_MODBUS_COMMAND_STATUS;
    response.status = OTA_MODBUS_STATUS_OK;
    response.session_id = 12U;
    response.update_state = 3U;
    response.target_slot = 1U;
    response.received_size = 65536U;
    response.expected_size = 861728U;
    response.current_baud_rate = 460800U;
    assert(ota_modbus_encode_response(&response, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(length == 23U);
    assert(ota_modbus_decode_response(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded.received_size == response.received_size);
    assert(decoded.current_baud_rate == response.current_baud_rate);

    response.command = OTA_MODBUS_COMMAND_DATA;
    response.status = OTA_MODBUS_STATUS_SEQUENCE;
    assert(ota_modbus_encode_response(&response, pdu, sizeof(pdu), &length) ==
           OTA_MODBUS_CODEC_OK);
    assert(length == 8U);
    assert(ota_modbus_decode_response(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_OK);
    assert(decoded.status == OTA_MODBUS_STATUS_SEQUENCE);

    pdu[1]++;
    assert(ota_modbus_decode_response(pdu, length, &decoded) ==
           OTA_MODBUS_CODEC_BAD_FRAME);
}

int main(void)
{
    test_crc32();
    test_hello_round_trip();
    test_baud_round_trip();
    test_descriptor_round_trip();
    test_data_round_trip();
    test_status_and_errors();
    puts("ota_modbus_protocol tests passed");
    return 0;
}
