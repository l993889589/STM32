#include "ota_modbus_protocol.h"

#include <string.h>

static uint16_t ota_modbus_get_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t ota_modbus_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void ota_modbus_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void ota_modbus_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void ota_modbus_encode_descriptor(
    const ota_modbus_descriptor_t *descriptor,
    uint8_t *data)
{
    ota_modbus_put_u32(&data[0], descriptor->state);
    ota_modbus_put_u32(&data[4], descriptor->image_version);
    ota_modbus_put_u32(&data[8], descriptor->image_size);
    ota_modbus_put_u32(&data[12], descriptor->image_crc32);
    ota_modbus_put_u32(&data[16], descriptor->image_flags);
    ota_modbus_put_u32(&data[20], descriptor->load_address);
    ota_modbus_put_u32(&data[24], descriptor->entry_address);
    memcpy(&data[28], descriptor->image_sha256,
           sizeof(descriptor->image_sha256));
    memcpy(&data[60], descriptor->signature,
           sizeof(descriptor->signature));
}

static void ota_modbus_decode_descriptor(
    const uint8_t *data,
    ota_modbus_descriptor_t *descriptor)
{
    descriptor->state = ota_modbus_get_u32(&data[0]);
    descriptor->image_version = ota_modbus_get_u32(&data[4]);
    descriptor->image_size = ota_modbus_get_u32(&data[8]);
    descriptor->image_crc32 = ota_modbus_get_u32(&data[12]);
    descriptor->image_flags = ota_modbus_get_u32(&data[16]);
    descriptor->load_address = ota_modbus_get_u32(&data[20]);
    descriptor->entry_address = ota_modbus_get_u32(&data[24]);
    memcpy(descriptor->image_sha256, &data[28],
           sizeof(descriptor->image_sha256));
    memcpy(descriptor->signature, &data[60],
           sizeof(descriptor->signature));
}

uint32_t ota_modbus_crc32(const uint8_t *data, size_t length)
{
    return ota_modbus_crc32_update(0U, data, length);
}

uint32_t ota_modbus_crc32_update(uint32_t crc,
                                 const uint8_t *data,
                                 size_t length)
{
    crc = ~crc;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    while (length-- != 0U)
    {
        crc ^= *data++;
        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                  ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }

    return ~crc;
}

ota_modbus_codec_status_t ota_modbus_encode_request(
    const ota_modbus_request_t *request,
    uint8_t *pdu,
    size_t pdu_size,
    size_t *pdu_length)
{
    size_t required;

    if ((request == NULL) || (pdu == NULL) || (pdu_length == NULL))
    {
        return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
    }

    switch (request->command)
    {
        case OTA_MODBUS_COMMAND_HELLO:
        case OTA_MODBUS_COMMAND_STATUS:
        case OTA_MODBUS_COMMAND_FINISH:
        case OTA_MODBUS_COMMAND_ABORT:
            required = 7U;
            break;

        case OTA_MODBUS_COMMAND_SET_BAUD:
            required = 15U;
            break;

        case OTA_MODBUS_COMMAND_SYNC:
            required = 11U;
            break;

        case OTA_MODBUS_COMMAND_BEGIN:
            required = 7U + OTA_MODBUS_DESCRIPTOR_SIZE;
            break;

        case OTA_MODBUS_COMMAND_DATA:
            if ((request->data_length > OTA_MODBUS_MAX_DATA_SIZE) ||
                ((request->data == NULL) && (request->data_length != 0U)))
            {
                return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
            }
            required = 16U + request->data_length;
            break;

        case OTA_MODBUS_COMMAND_ACTIVATE:
            required = 9U;
            break;

        default:
            return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
    }

    if ((required > OTA_MODBUS_MAX_PDU_SIZE) || (pdu_size < required))
    {
        return OTA_MODBUS_CODEC_BUFFER_TOO_SMALL;
    }

    pdu[0] = OTA_MODBUS_FUNCTION_CODE;
    pdu[1] = OTA_MODBUS_PROTOCOL_VERSION;
    pdu[2] = request->command;
    ota_modbus_put_u32(&pdu[3], request->session_id);

    switch (request->command)
    {
        case OTA_MODBUS_COMMAND_SET_BAUD:
            ota_modbus_put_u32(&pdu[7], request->baud_rate);
            ota_modbus_put_u16(&pdu[11], request->switch_delay_ms);
            ota_modbus_put_u16(&pdu[13], request->fallback_timeout_ms);
            break;

        case OTA_MODBUS_COMMAND_SYNC:
            ota_modbus_put_u32(&pdu[7], request->baud_rate);
            break;

        case OTA_MODBUS_COMMAND_BEGIN:
            ota_modbus_encode_descriptor(&request->descriptor, &pdu[7]);
            break;

        case OTA_MODBUS_COMMAND_DATA:
            ota_modbus_put_u32(&pdu[7], request->offset);
            pdu[11] = (uint8_t)request->data_length;
            memcpy(&pdu[12], request->data, request->data_length);
            ota_modbus_put_u32(&pdu[12U + request->data_length],
                               ota_modbus_crc32(request->data,
                                                request->data_length));
            break;

        case OTA_MODBUS_COMMAND_ACTIVATE:
            ota_modbus_put_u16(&pdu[7], request->reboot_delay_ms);
            break;

        default:
            break;
    }

    *pdu_length = required;
    return OTA_MODBUS_CODEC_OK;
}

ota_modbus_codec_status_t ota_modbus_decode_request(
    const uint8_t *pdu,
    size_t pdu_length,
    ota_modbus_request_t *request)
{
    size_t expected;

    if ((pdu == NULL) || (request == NULL))
    {
        return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
    }
    if ((pdu_length < 7U) || (pdu_length > OTA_MODBUS_MAX_PDU_SIZE) ||
        (pdu[0] != OTA_MODBUS_FUNCTION_CODE) ||
        (pdu[1] != OTA_MODBUS_PROTOCOL_VERSION))
    {
        return OTA_MODBUS_CODEC_BAD_FRAME;
    }

    memset(request, 0, sizeof(*request));
    request->command = pdu[2];
    request->session_id = ota_modbus_get_u32(&pdu[3]);

    switch (request->command)
    {
        case OTA_MODBUS_COMMAND_HELLO:
        case OTA_MODBUS_COMMAND_STATUS:
        case OTA_MODBUS_COMMAND_FINISH:
        case OTA_MODBUS_COMMAND_ABORT:
            expected = 7U;
            break;

        case OTA_MODBUS_COMMAND_SET_BAUD:
            expected = 15U;
            if (pdu_length == expected)
            {
                request->baud_rate = ota_modbus_get_u32(&pdu[7]);
                request->switch_delay_ms = ota_modbus_get_u16(&pdu[11]);
                request->fallback_timeout_ms = ota_modbus_get_u16(&pdu[13]);
            }
            break;

        case OTA_MODBUS_COMMAND_SYNC:
            expected = 11U;
            if (pdu_length == expected)
            {
                request->baud_rate = ota_modbus_get_u32(&pdu[7]);
            }
            break;

        case OTA_MODBUS_COMMAND_BEGIN:
            expected = 7U + OTA_MODBUS_DESCRIPTOR_SIZE;
            if (pdu_length == expected)
            {
                ota_modbus_decode_descriptor(&pdu[7], &request->descriptor);
            }
            break;

        case OTA_MODBUS_COMMAND_DATA:
            if (pdu_length < 16U)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            request->offset = ota_modbus_get_u32(&pdu[7]);
            request->data_length = pdu[11];
            expected = 16U + request->data_length;
            if ((request->data_length > OTA_MODBUS_MAX_DATA_SIZE) ||
                (pdu_length != expected))
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            request->data = &pdu[12];
            request->data_crc32 =
                ota_modbus_get_u32(&pdu[12U + request->data_length]);
            break;

        case OTA_MODBUS_COMMAND_ACTIVATE:
            expected = 9U;
            if (pdu_length == expected)
            {
                request->reboot_delay_ms = ota_modbus_get_u16(&pdu[7]);
            }
            break;

        default:
            return OTA_MODBUS_CODEC_BAD_FRAME;
    }

    return (pdu_length == expected) ?
           OTA_MODBUS_CODEC_OK : OTA_MODBUS_CODEC_BAD_FRAME;
}

ota_modbus_codec_status_t ota_modbus_encode_response(
    const ota_modbus_response_t *response,
    uint8_t *pdu,
    size_t pdu_size,
    size_t *pdu_length)
{
    size_t required = 8U;

    if ((response == NULL) || (pdu == NULL) || (pdu_length == NULL))
    {
        return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
    }

    if (response->status == OTA_MODBUS_STATUS_OK)
    {
        switch (response->command)
        {
            case OTA_MODBUS_COMMAND_HELLO:
                if ((response->baud_rate_count > OTA_MODBUS_MAX_BAUD_RATE_COUNT) ||
                    (response->max_data_size > OTA_MODBUS_MAX_DATA_SIZE))
                {
                    return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
                }
                required = 19U + ((size_t)response->baud_rate_count * 4U);
                break;

            case OTA_MODBUS_COMMAND_SET_BAUD:
                required = 16U;
                break;

            case OTA_MODBUS_COMMAND_SYNC:
                required = 12U;
                break;

            case OTA_MODBUS_COMMAND_BEGIN:
                required = 17U;
                break;

            case OTA_MODBUS_COMMAND_DATA:
                required = 12U;
                break;

            case OTA_MODBUS_COMMAND_STATUS:
            case OTA_MODBUS_COMMAND_FINISH:
                required = 23U;
                break;

            case OTA_MODBUS_COMMAND_ACTIVATE:
            case OTA_MODBUS_COMMAND_ABORT:
                required = 8U;
                break;

            default:
                return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
        }
    }

    if ((required > OTA_MODBUS_MAX_PDU_SIZE) || (pdu_size < required))
    {
        return OTA_MODBUS_CODEC_BUFFER_TOO_SMALL;
    }

    pdu[0] = OTA_MODBUS_FUNCTION_CODE;
    pdu[1] = OTA_MODBUS_PROTOCOL_VERSION;
    pdu[2] = response->command;
    pdu[3] = response->status;
    ota_modbus_put_u32(&pdu[4], response->session_id);

    if (response->status == OTA_MODBUS_STATUS_OK)
    {
        switch (response->command)
        {
            case OTA_MODBUS_COMMAND_HELLO:
                ota_modbus_put_u32(&pdu[8], response->capability_flags);
                ota_modbus_put_u16(&pdu[12], response->max_data_size);
                ota_modbus_put_u32(&pdu[14], response->current_baud_rate);
                pdu[18] = response->baud_rate_count;
                for (uint32_t index = 0U;
                     index < response->baud_rate_count;
                     index++)
                {
                    ota_modbus_put_u32(&pdu[19U + (index * 4U)],
                                       response->baud_rates[index]);
                }
                break;

            case OTA_MODBUS_COMMAND_SET_BAUD:
                ota_modbus_put_u32(&pdu[8], response->baud_rate);
                ota_modbus_put_u16(&pdu[12], response->switch_delay_ms);
                ota_modbus_put_u16(&pdu[14], response->fallback_timeout_ms);
                break;

            case OTA_MODBUS_COMMAND_SYNC:
                ota_modbus_put_u32(&pdu[8], response->baud_rate);
                break;

            case OTA_MODBUS_COMMAND_BEGIN:
                pdu[8] = response->target_slot;
                ota_modbus_put_u32(&pdu[9], response->received_size);
                ota_modbus_put_u32(&pdu[13], response->expected_size);
                break;

            case OTA_MODBUS_COMMAND_DATA:
                ota_modbus_put_u32(&pdu[8], response->received_size);
                break;

            case OTA_MODBUS_COMMAND_STATUS:
            case OTA_MODBUS_COMMAND_FINISH:
                pdu[8] = response->update_state;
                pdu[9] = response->target_slot;
                pdu[10] = response->last_error;
                ota_modbus_put_u32(&pdu[11], response->received_size);
                ota_modbus_put_u32(&pdu[15], response->expected_size);
                ota_modbus_put_u32(&pdu[19], response->current_baud_rate);
                break;

            default:
                break;
        }
    }

    *pdu_length = required;
    return OTA_MODBUS_CODEC_OK;
}

ota_modbus_codec_status_t ota_modbus_decode_response(
    const uint8_t *pdu,
    size_t pdu_length,
    ota_modbus_response_t *response)
{
    size_t expected;

    if ((pdu == NULL) || (response == NULL))
    {
        return OTA_MODBUS_CODEC_INVALID_ARGUMENT;
    }
    if ((pdu_length < 8U) || (pdu_length > OTA_MODBUS_MAX_PDU_SIZE) ||
        (pdu[0] != OTA_MODBUS_FUNCTION_CODE) ||
        (pdu[1] != OTA_MODBUS_PROTOCOL_VERSION))
    {
        return OTA_MODBUS_CODEC_BAD_FRAME;
    }

    memset(response, 0, sizeof(*response));
    response->command = pdu[2];
    response->status = pdu[3];
    response->session_id = ota_modbus_get_u32(&pdu[4]);

    if (response->status != OTA_MODBUS_STATUS_OK)
    {
        return (pdu_length == 8U) ?
               OTA_MODBUS_CODEC_OK : OTA_MODBUS_CODEC_BAD_FRAME;
    }

    switch (response->command)
    {
        case OTA_MODBUS_COMMAND_HELLO:
            if (pdu_length < 19U)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            response->capability_flags = ota_modbus_get_u32(&pdu[8]);
            response->max_data_size = ota_modbus_get_u16(&pdu[12]);
            response->current_baud_rate = ota_modbus_get_u32(&pdu[14]);
            response->baud_rate_count = pdu[18];
            expected = 19U + ((size_t)response->baud_rate_count * 4U);
            if ((response->baud_rate_count > OTA_MODBUS_MAX_BAUD_RATE_COUNT) ||
                (response->max_data_size > OTA_MODBUS_MAX_DATA_SIZE) ||
                (pdu_length != expected))
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            for (uint32_t index = 0U;
                 index < response->baud_rate_count;
                 index++)
            {
                response->baud_rates[index] =
                    ota_modbus_get_u32(&pdu[19U + (index * 4U)]);
            }
            break;

        case OTA_MODBUS_COMMAND_SET_BAUD:
            expected = 16U;
            if (pdu_length != expected)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            response->baud_rate = ota_modbus_get_u32(&pdu[8]);
            response->switch_delay_ms = ota_modbus_get_u16(&pdu[12]);
            response->fallback_timeout_ms = ota_modbus_get_u16(&pdu[14]);
            break;

        case OTA_MODBUS_COMMAND_SYNC:
            expected = 12U;
            if (pdu_length != expected)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            response->baud_rate = ota_modbus_get_u32(&pdu[8]);
            break;

        case OTA_MODBUS_COMMAND_BEGIN:
            expected = 17U;
            if (pdu_length != expected)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            response->target_slot = pdu[8];
            response->received_size = ota_modbus_get_u32(&pdu[9]);
            response->expected_size = ota_modbus_get_u32(&pdu[13]);
            break;

        case OTA_MODBUS_COMMAND_DATA:
            expected = 12U;
            if (pdu_length != expected)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            response->received_size = ota_modbus_get_u32(&pdu[8]);
            break;

        case OTA_MODBUS_COMMAND_STATUS:
        case OTA_MODBUS_COMMAND_FINISH:
            expected = 23U;
            if (pdu_length != expected)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            response->update_state = pdu[8];
            response->target_slot = pdu[9];
            response->last_error = pdu[10];
            response->received_size = ota_modbus_get_u32(&pdu[11]);
            response->expected_size = ota_modbus_get_u32(&pdu[15]);
            response->current_baud_rate = ota_modbus_get_u32(&pdu[19]);
            break;

        case OTA_MODBUS_COMMAND_ACTIVATE:
        case OTA_MODBUS_COMMAND_ABORT:
            if (pdu_length != 8U)
            {
                return OTA_MODBUS_CODEC_BAD_FRAME;
            }
            break;

        default:
            return OTA_MODBUS_CODEC_BAD_FRAME;
    }

    return OTA_MODBUS_CODEC_OK;
}
