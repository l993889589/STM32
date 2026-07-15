#ifndef OTA_MODBUS_PROTOCOL_H
#define OTA_MODBUS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define OTA_MODBUS_FUNCTION_CODE          0x41U
#define OTA_MODBUS_PROTOCOL_VERSION       1U
#define OTA_MODBUS_MAX_PDU_SIZE           253U
#define OTA_MODBUS_MAX_DATA_SIZE          232U
#define OTA_MODBUS_DESCRIPTOR_SIZE        124U
#define OTA_MODBUS_MAX_BAUD_RATE_COUNT    4U

#define OTA_MODBUS_CAP_BAUD_SWITCH        0x00000001UL
#define OTA_MODBUS_CAP_RESUME             0x00000002UL
#define OTA_MODBUS_CAP_SIGNED_IMAGE       0x00000004UL
#define OTA_MODBUS_CAP_AB_SLOTS           0x00000008UL

typedef enum
{
    OTA_MODBUS_COMMAND_HELLO = 1,
    OTA_MODBUS_COMMAND_SET_BAUD = 2,
    OTA_MODBUS_COMMAND_SYNC = 3,
    OTA_MODBUS_COMMAND_BEGIN = 4,
    OTA_MODBUS_COMMAND_DATA = 5,
    OTA_MODBUS_COMMAND_STATUS = 6,
    OTA_MODBUS_COMMAND_FINISH = 7,
    OTA_MODBUS_COMMAND_ACTIVATE = 8,
    OTA_MODBUS_COMMAND_ABORT = 9
} ota_modbus_command_t;

typedef enum
{
    OTA_MODBUS_STATUS_OK = 0,
    OTA_MODBUS_STATUS_BAD_VERSION = 1,
    OTA_MODBUS_STATUS_BAD_COMMAND = 2,
    OTA_MODBUS_STATUS_BAD_SESSION = 3,
    OTA_MODBUS_STATUS_BAD_LENGTH = 4,
    OTA_MODBUS_STATUS_BAD_CRC = 5,
    OTA_MODBUS_STATUS_BAD_STATE = 6,
    OTA_MODBUS_STATUS_BAD_RANGE = 7,
    OTA_MODBUS_STATUS_SEQUENCE = 8,
    OTA_MODBUS_STATUS_BUSY = 9,
    OTA_MODBUS_STATUS_FLASH_ERROR = 10,
    OTA_MODBUS_STATUS_VERIFY_FAILED = 11,
    OTA_MODBUS_STATUS_VERSION_ROLLBACK = 12,
    OTA_MODBUS_STATUS_UNSUPPORTED_BAUD = 13,
    OTA_MODBUS_STATUS_TIMEOUT = 14,
    OTA_MODBUS_STATUS_INTERNAL_ERROR = 15
} ota_modbus_status_t;

typedef enum
{
    OTA_MODBUS_CODEC_OK = 0,
    OTA_MODBUS_CODEC_INVALID_ARGUMENT = -1,
    OTA_MODBUS_CODEC_BUFFER_TOO_SMALL = -2,
    OTA_MODBUS_CODEC_BAD_FRAME = -3
} ota_modbus_codec_status_t;

typedef struct
{
    uint32_t state;
    uint32_t image_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_flags;
    uint32_t load_address;
    uint32_t entry_address;
    uint8_t image_sha256[32];
    uint8_t signature[64];
} ota_modbus_descriptor_t;

typedef struct
{
    uint8_t command;
    uint32_t session_id;
    uint32_t baud_rate;
    uint16_t switch_delay_ms;
    uint16_t fallback_timeout_ms;
    ota_modbus_descriptor_t descriptor;
    uint32_t offset;
    const uint8_t *data;
    uint16_t data_length;
    uint32_t data_crc32;
    uint16_t reboot_delay_ms;
} ota_modbus_request_t;

typedef struct
{
    uint8_t command;
    uint8_t status;
    uint32_t session_id;
    uint32_t capability_flags;
    uint16_t max_data_size;
    uint32_t current_baud_rate;
    uint8_t baud_rate_count;
    uint32_t baud_rates[OTA_MODBUS_MAX_BAUD_RATE_COUNT];
    uint32_t baud_rate;
    uint16_t switch_delay_ms;
    uint16_t fallback_timeout_ms;
    uint8_t update_state;
    uint8_t target_slot;
    uint8_t last_error;
    uint32_t received_size;
    uint32_t expected_size;
} ota_modbus_response_t;

uint32_t ota_modbus_crc32(const uint8_t *data, size_t length);
uint32_t ota_modbus_crc32_update(uint32_t crc,
                                 const uint8_t *data,
                                 size_t length);

ota_modbus_codec_status_t ota_modbus_encode_request(
    const ota_modbus_request_t *request,
    uint8_t *pdu,
    size_t pdu_size,
    size_t *pdu_length);

ota_modbus_codec_status_t ota_modbus_decode_request(
    const uint8_t *pdu,
    size_t pdu_length,
    ota_modbus_request_t *request);

ota_modbus_codec_status_t ota_modbus_encode_response(
    const ota_modbus_response_t *response,
    uint8_t *pdu,
    size_t pdu_size,
    size_t *pdu_length);

ota_modbus_codec_status_t ota_modbus_decode_response(
    const uint8_t *pdu,
    size_t pdu_length,
    ota_modbus_response_t *response);

#endif /* OTA_MODBUS_PROTOCOL_H */
