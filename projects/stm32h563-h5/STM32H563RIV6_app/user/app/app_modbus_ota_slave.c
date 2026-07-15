/**
 * @file app_modbus_ota_slave.c
 * @brief Signed A/B firmware transport over one user-defined Modbus function.
 */

#include "app_modbus_ota_slave.h"

#include <string.h>

#include "app_firmware_update_service.h"
#include "ota_boot_control.h"
#include "ota_modbus_protocol.h"
#include "stm32h5xx_hal.h"

#define APP_MODBUS_OTA_MIN_SWITCH_DELAY_MS       20U
#define APP_MODBUS_OTA_MAX_SWITCH_DELAY_MS      500U
#define APP_MODBUS_OTA_MIN_LEASE_MS             500U
#define APP_MODBUS_OTA_MAX_LEASE_MS           10000U
#define APP_MODBUS_OTA_DEFAULT_LEASE_MS        5000U
#define APP_MODBUS_OTA_MIN_REBOOT_DELAY_MS       20U
#define APP_MODBUS_OTA_MAX_REBOOT_DELAY_MS     1000U

typedef enum
{
    APP_MODBUS_OTA_STATE_IDLE = 0,
    APP_MODBUS_OTA_STATE_NEGOTIATING = 1,
    APP_MODBUS_OTA_STATE_DOWNLOADING = 2,
    APP_MODBUS_OTA_STATE_VERIFIED = 3,
    APP_MODBUS_OTA_STATE_PENDING_RESET = 4,
    APP_MODBUS_OTA_STATE_ERROR = 5
} app_modbus_ota_state_t;

static const uint32_t app_modbus_ota_baud_rates[] =
{
    115200U,
    230400U,
    460800U,
    921600U
};

static uint32_t app_modbus_ota_base_baud_rate;
static uint32_t app_modbus_ota_current_baud_rate;
static uint32_t app_modbus_ota_session_id;
static uint32_t app_modbus_ota_last_activity_ms;
static uint32_t app_modbus_ota_baud_lease_ms;
static uint8_t app_modbus_ota_state;
static uint8_t app_modbus_ota_last_error;
static uint8_t app_modbus_ota_ready_to_activate;
static uint8_t app_modbus_ota_baud_change_pending;

static uint8_t app_modbus_ota_baud_is_supported(uint32_t baud_rate)
{
    for(uint32_t index = 0U;
        index < (sizeof(app_modbus_ota_baud_rates) /
                 sizeof(app_modbus_ota_baud_rates[0]));
        index++)
    {
        if(app_modbus_ota_baud_rates[index] == baud_rate)
            return 1U;
    }
    return 0U;
}

static uint16_t app_modbus_ota_clamp_u16(uint16_t value,
                                         uint16_t minimum,
                                         uint16_t maximum)
{
    if(value < minimum)
        return minimum;
    if(value > maximum)
        return maximum;
    return value;
}

static uint8_t app_modbus_ota_map_update_status(
    ota_firmware_update_status_t status)
{
    switch(status)
    {
        case OTA_FIRMWARE_UPDATE_OK:
            return OTA_MODBUS_STATUS_OK;
        case OTA_FIRMWARE_UPDATE_INVALID_ARGUMENT:
            return OTA_MODBUS_STATUS_BAD_RANGE;
        case OTA_FIRMWARE_UPDATE_BUSY:
            return OTA_MODBUS_STATUS_BUSY;
        case OTA_FIRMWARE_UPDATE_BAD_STATE:
        case OTA_FIRMWARE_UPDATE_NOT_PROVISIONED:
            return OTA_MODBUS_STATUS_BAD_STATE;
        case OTA_FIRMWARE_UPDATE_BAD_RANGE:
            return OTA_MODBUS_STATUS_BAD_RANGE;
        case OTA_FIRMWARE_UPDATE_SEQUENCE:
            return OTA_MODBUS_STATUS_SEQUENCE;
        case OTA_FIRMWARE_UPDATE_IO_ERROR:
        case OTA_FIRMWARE_UPDATE_CONTROL_ERROR:
            return OTA_MODBUS_STATUS_FLASH_ERROR;
        case OTA_FIRMWARE_UPDATE_VERIFY_FAILED:
        case OTA_FIRMWARE_UPDATE_CRC_MISMATCH:
        case OTA_FIRMWARE_UPDATE_SHA256_MISMATCH:
            return OTA_MODBUS_STATUS_VERIFY_FAILED;
        case OTA_FIRMWARE_UPDATE_VERSION_ROLLBACK:
            return OTA_MODBUS_STATUS_VERSION_ROLLBACK;
        default:
            return OTA_MODBUS_STATUS_INTERNAL_ERROR;
    }
}

static void app_modbus_ota_copy_descriptor(
    const ota_modbus_descriptor_t *source,
    ota_firmware_descriptor_t *destination)
{
    memset(destination, 0, sizeof(*destination));
    destination->state = source->state;
    destination->image_version = source->image_version;
    destination->image_size = source->image_size;
    destination->image_crc32 = source->image_crc32;
    destination->image_flags = source->image_flags;
    destination->load_address = source->load_address;
    destination->entry_address = source->entry_address;
    memcpy(destination->image_sha256, source->image_sha256,
           sizeof(destination->image_sha256));
    memcpy(destination->signature, source->signature,
           sizeof(destination->signature));
}

static void app_modbus_ota_fill_progress(ota_modbus_response_t *response)
{
    uint8_t is_active = 0U;
    uint32_t target_slot = (uint32_t)OTA_FIRMWARE_SLOT_NONE;

    app_firmware_update_service_get_progress(&is_active,
                                             &target_slot,
                                             &response->received_size,
                                             &response->expected_size);
    response->update_state = app_modbus_ota_state;
    response->target_slot = (uint8_t)target_slot;
    response->last_error = app_modbus_ota_last_error;
    response->current_baud_rate = app_modbus_ota_current_baud_rate;
    if(is_active != 0U)
        response->update_state = APP_MODBUS_OTA_STATE_DOWNLOADING;
}

static void app_modbus_ota_fill_hello(ota_modbus_response_t *response)
{
    response->capability_flags = OTA_MODBUS_CAP_BAUD_SWITCH |
                                 OTA_MODBUS_CAP_RESUME |
                                 OTA_MODBUS_CAP_SIGNED_IMAGE |
                                 OTA_MODBUS_CAP_AB_SLOTS;
    response->max_data_size = OTA_MODBUS_MAX_DATA_SIZE;
    response->current_baud_rate = app_modbus_ota_current_baud_rate;
    response->baud_rate_count =
        (uint8_t)(sizeof(app_modbus_ota_baud_rates) /
                  sizeof(app_modbus_ota_baud_rates[0]));
    for(uint32_t index = 0U; index < response->baud_rate_count; index++)
        response->baud_rates[index] = app_modbus_ota_baud_rates[index];
}

int app_modbus_ota_slave_init(uint32_t base_baud_rate)
{
    if(app_modbus_ota_baud_is_supported(base_baud_rate) == 0U)
        return -1;
    if(app_firmware_update_service_init() != OTA_FIRMWARE_UPDATE_OK)
        return -1;

    app_modbus_ota_base_baud_rate = base_baud_rate;
    app_modbus_ota_current_baud_rate = base_baud_rate;
    app_modbus_ota_baud_lease_ms = APP_MODBUS_OTA_DEFAULT_LEASE_MS;
    app_modbus_ota_state = APP_MODBUS_OTA_STATE_IDLE;
    app_modbus_ota_last_activity_ms = HAL_GetTick();
    return 0;
}

uint8_t app_modbus_ota_slave_process(
    const uint8_t *request_pdu,
    size_t request_length,
    uint8_t *response_pdu,
    size_t response_size,
    size_t *response_length,
    app_modbus_ota_slave_action_t *action)
{
    ota_modbus_request_t request;
    ota_modbus_response_t response;
    ota_modbus_codec_status_t codec_status;
    uint8_t is_active = 0U;
    uint8_t clear_session_after_reply = 0U;

    if(request_pdu == NULL || request_length == 0U ||
       request_pdu[0] != OTA_MODBUS_FUNCTION_CODE)
    {
        return 0U;
    }
    if(response_pdu == NULL || response_length == NULL || action == NULL)
        return 1U;

    memset(action, 0, sizeof(*action));
    memset(&response, 0, sizeof(response));
    response.command = (request_length > 2U) ? request_pdu[2] : 0U;
    response.status = OTA_MODBUS_STATUS_BAD_LENGTH;

    if(request_length > 1U &&
       request_pdu[1] != OTA_MODBUS_PROTOCOL_VERSION)
    {
        response.status = OTA_MODBUS_STATUS_BAD_VERSION;
        goto encode_reply;
    }

    codec_status = ota_modbus_decode_request(request_pdu,
                                             request_length,
                                             &request);
    if(codec_status != OTA_MODBUS_CODEC_OK)
        goto encode_reply;

    response.command = request.command;
    response.session_id = request.session_id;
    app_firmware_update_service_get_progress(&is_active, NULL, NULL, NULL);

    if(request.command == OTA_MODBUS_COMMAND_HELLO)
    {
        if(request.session_id == 0U)
        {
            response.status = OTA_MODBUS_STATUS_BAD_SESSION;
        }
        else if(is_active != 0U &&
                app_modbus_ota_session_id != 0U &&
                app_modbus_ota_session_id != request.session_id)
        {
            response.status = OTA_MODBUS_STATUS_BUSY;
        }
        else
        {
            app_modbus_ota_session_id = request.session_id;
            app_modbus_ota_state = (is_active != 0U) ?
                                   APP_MODBUS_OTA_STATE_DOWNLOADING :
                                   APP_MODBUS_OTA_STATE_NEGOTIATING;
            app_modbus_ota_last_error = OTA_MODBUS_STATUS_OK;
            response.status = OTA_MODBUS_STATUS_OK;
            app_modbus_ota_fill_hello(&response);
        }
        app_modbus_ota_last_activity_ms = HAL_GetTick();
        goto encode_reply;
    }

    if(request.session_id == 0U ||
       request.session_id != app_modbus_ota_session_id)
    {
        response.status = OTA_MODBUS_STATUS_BAD_SESSION;
        goto encode_reply;
    }

    app_modbus_ota_last_activity_ms = HAL_GetTick();
    response.status = OTA_MODBUS_STATUS_OK;

    switch(request.command)
    {
        case OTA_MODBUS_COMMAND_SET_BAUD:
            if(app_modbus_ota_baud_is_supported(request.baud_rate) == 0U)
            {
                response.status = OTA_MODBUS_STATUS_UNSUPPORTED_BAUD;
                break;
            }
            if(app_modbus_ota_baud_change_pending != 0U)
            {
                response.status = OTA_MODBUS_STATUS_BUSY;
                break;
            }
            response.baud_rate = request.baud_rate;
            response.switch_delay_ms = app_modbus_ota_clamp_u16(
                request.switch_delay_ms,
                APP_MODBUS_OTA_MIN_SWITCH_DELAY_MS,
                APP_MODBUS_OTA_MAX_SWITCH_DELAY_MS);
            response.fallback_timeout_ms = app_modbus_ota_clamp_u16(
                request.fallback_timeout_ms,
                APP_MODBUS_OTA_MIN_LEASE_MS,
                APP_MODBUS_OTA_MAX_LEASE_MS);
            app_modbus_ota_baud_lease_ms = response.fallback_timeout_ms;
            if(request.baud_rate != app_modbus_ota_current_baud_rate)
            {
                action->change_baud = 1U;
                action->baud_rate = request.baud_rate;
                action->delay_ms = response.switch_delay_ms;
                app_modbus_ota_baud_change_pending = 1U;
            }
            break;

        case OTA_MODBUS_COMMAND_SYNC:
            if(request.baud_rate != app_modbus_ota_current_baud_rate ||
               app_modbus_ota_baud_change_pending != 0U)
            {
                response.status = OTA_MODBUS_STATUS_BAD_STATE;
            }
            else
            {
                response.baud_rate = app_modbus_ota_current_baud_rate;
            }
            break;

        case OTA_MODBUS_COMMAND_BEGIN:
        {
            ota_firmware_descriptor_t descriptor;
            ota_firmware_update_status_t status;

            app_modbus_ota_copy_descriptor(&request.descriptor, &descriptor);
            status = app_firmware_update_service_begin(&descriptor);
            response.status = app_modbus_ota_map_update_status(status);
            app_modbus_ota_last_error = response.status;
            if(response.status == OTA_MODBUS_STATUS_OK)
            {
                app_modbus_ota_state = APP_MODBUS_OTA_STATE_DOWNLOADING;
                app_modbus_ota_ready_to_activate = 0U;
                app_modbus_ota_fill_progress(&response);
            }
            else
            {
                app_modbus_ota_state = APP_MODBUS_OTA_STATE_ERROR;
            }
            break;
        }

        case OTA_MODBUS_COMMAND_DATA:
            if(request.data_crc32 !=
               ota_modbus_crc32(request.data, request.data_length))
            {
                response.status = OTA_MODBUS_STATUS_BAD_CRC;
            }
            else
            {
                response.status = app_modbus_ota_map_update_status(
                    app_firmware_update_service_write(request.offset,
                                                      request.data,
                                                      request.data_length));
            }
            app_modbus_ota_last_error = response.status;
            if(response.status == OTA_MODBUS_STATUS_OK)
            {
                app_modbus_ota_fill_progress(&response);
            }
            else if(response.status != OTA_MODBUS_STATUS_SEQUENCE)
            {
                app_modbus_ota_state = APP_MODBUS_OTA_STATE_ERROR;
            }
            break;

        case OTA_MODBUS_COMMAND_STATUS:
            app_modbus_ota_fill_progress(&response);
            break;

        case OTA_MODBUS_COMMAND_FINISH:
            response.status = app_modbus_ota_map_update_status(
                app_firmware_update_service_finish());
            app_modbus_ota_last_error = response.status;
            if(response.status == OTA_MODBUS_STATUS_OK)
            {
                app_modbus_ota_state = APP_MODBUS_OTA_STATE_VERIFIED;
                app_modbus_ota_ready_to_activate = 1U;
                app_modbus_ota_fill_progress(&response);
            }
            else
            {
                app_modbus_ota_state = APP_MODBUS_OTA_STATE_ERROR;
            }
            break;

        case OTA_MODBUS_COMMAND_ACTIVATE:
            if(app_modbus_ota_ready_to_activate == 0U)
            {
                response.status = OTA_MODBUS_STATUS_BAD_STATE;
                break;
            }
            app_modbus_ota_state = APP_MODBUS_OTA_STATE_PENDING_RESET;
            action->reset_target = 1U;
            action->delay_ms = app_modbus_ota_clamp_u16(
                request.reboot_delay_ms,
                APP_MODBUS_OTA_MIN_REBOOT_DELAY_MS,
                APP_MODBUS_OTA_MAX_REBOOT_DELAY_MS);
            break;

        case OTA_MODBUS_COMMAND_ABORT:
            app_firmware_update_service_get_progress(&is_active,
                                                     NULL,
                                                     NULL,
                                                     NULL);
            if(is_active != 0U)
            {
                response.status = app_modbus_ota_map_update_status(
                    app_firmware_update_service_abort());
            }
            if(response.status == OTA_MODBUS_STATUS_OK)
            {
                app_modbus_ota_state = APP_MODBUS_OTA_STATE_IDLE;
                app_modbus_ota_last_error = OTA_MODBUS_STATUS_OK;
                app_modbus_ota_ready_to_activate = 0U;
                if(app_modbus_ota_current_baud_rate !=
                   app_modbus_ota_base_baud_rate)
                {
                    action->change_baud = 1U;
                    action->baud_rate = app_modbus_ota_base_baud_rate;
                    action->delay_ms = APP_MODBUS_OTA_MIN_SWITCH_DELAY_MS;
                    app_modbus_ota_baud_change_pending = 1U;
                }
                clear_session_after_reply = 1U;
            }
            break;

        default:
            response.status = OTA_MODBUS_STATUS_BAD_COMMAND;
            break;
    }

    /*
     * BEGIN and FINISH may spend several seconds erasing or verifying NOR.
     * Renew the high-speed lease after that work, otherwise the server can
     * fall back to the base baud immediately after producing its reply.
     */
    app_modbus_ota_last_activity_ms = HAL_GetTick();

encode_reply:
    if(ota_modbus_encode_response(&response,
                                  response_pdu,
                                  response_size,
                                  response_length) != OTA_MODBUS_CODEC_OK)
    {
        *response_length = 0U;
    }
    if(clear_session_after_reply != 0U)
        app_modbus_ota_session_id = 0U;
    return 1U;
}

uint8_t app_modbus_ota_slave_poll(app_modbus_ota_slave_action_t *action)
{
    if(action == NULL)
        return 0U;

    memset(action, 0, sizeof(*action));
    if(app_modbus_ota_baud_change_pending == 0U &&
       app_modbus_ota_current_baud_rate != app_modbus_ota_base_baud_rate &&
       (uint32_t)(HAL_GetTick() - app_modbus_ota_last_activity_ms) >=
           app_modbus_ota_baud_lease_ms)
    {
        action->change_baud = 1U;
        action->baud_rate = app_modbus_ota_base_baud_rate;
        app_modbus_ota_baud_change_pending = 1U;
        app_modbus_ota_session_id = 0U;
        return 1U;
    }

    return 0U;
}

void app_modbus_ota_slave_baud_changed(uint32_t baud_rate, uint8_t success)
{
    app_modbus_ota_baud_change_pending = 0U;
    if(success != 0U)
    {
        app_modbus_ota_current_baud_rate = baud_rate;
        app_modbus_ota_last_activity_ms = HAL_GetTick();
    }
    else
    {
        app_modbus_ota_session_id = 0U;
    }
}
