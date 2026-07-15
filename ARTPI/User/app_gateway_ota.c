#include "app_gateway_ota.h"

#include <stddef.h>
#include <string.h>

#include "bsp.h"
#include "gateway_ota_format.h"
#include "ota_modbus_protocol.h"
#include "ota_sha256.h"
#include "tx_api.h"

#define APP_GATEWAY_OTA_VERIFY_BUFFER_SIZE 1024U

typedef enum
{
    APP_GATEWAY_OTA_ERROR_NONE = 0,
    APP_GATEWAY_OTA_ERROR_MANIFEST = 1,
    APP_GATEWAY_OTA_ERROR_RANGE = 2,
    APP_GATEWAY_OTA_ERROR_SEQUENCE = 3,
    APP_GATEWAY_OTA_ERROR_FLASH = 4,
    APP_GATEWAY_OTA_ERROR_CRC = 5,
    APP_GATEWAY_OTA_ERROR_SHA256 = 6,
    APP_GATEWAY_OTA_ERROR_STATE = 7
} app_gateway_ota_error_t;

static TX_MUTEX gateway_ota_mutex;
static gateway_ota_manifest_t gateway_ota_manifest;
static uint8_t gateway_ota_verify_buffer[APP_GATEWAY_OTA_VERIFY_BUFFER_SIZE];
static app_gateway_ota_status_t gateway_ota_status;
static uint8_t gateway_ota_initialized;

static uint8_t app_gateway_ota_manifest_valid(
    const gateway_ota_manifest_t *manifest);
static HAL_StatusTypeDef app_gateway_ota_verify_image(void);
static HAL_StatusTypeDef app_gateway_ota_read_marker(uint32_t address,
                                                      uint32_t *marker);
static HAL_StatusTypeDef app_gateway_ota_set_marker(uint32_t address);

HAL_StatusTypeDef app_gateway_ota_init(void)
{
    uint32_t complete = GATEWAY_OTA_MARKER_ERASED;
    uint32_t request = GATEWAY_OTA_MARKER_ERASED;
    uint32_t consumed = GATEWAY_OTA_MARKER_ERASED;

    if(gateway_ota_initialized != 0U)
    {
        return HAL_OK;
    }
    if(tx_mutex_create(&gateway_ota_mutex,
                       "gateway_ota",
                       TX_INHERIT) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }

    memset(&gateway_ota_status, 0, sizeof(gateway_ota_status));
    gateway_ota_status.boot_error = RTC->BKP7R;
    gateway_ota_status.active_version = RTC->BKP8R;
    gateway_ota_status.pending_version = RTC->BKP9R;
    gateway_ota_status.boot_sequence = RTC->BKP10R;

    if(bsp_w25q128_read(GATEWAY_OTA_STAGE_ADDRESS,
                        (uint8_t *)&gateway_ota_manifest,
                        sizeof(gateway_ota_manifest)) == HAL_OK &&
       app_gateway_ota_manifest_valid(&gateway_ota_manifest) != 0U &&
       app_gateway_ota_read_marker(GATEWAY_OTA_STAGE_COMPLETE_ADDRESS,
                                   &complete) == HAL_OK &&
       app_gateway_ota_read_marker(GATEWAY_OTA_STAGE_REQUEST_ADDRESS,
                                   &request) == HAL_OK &&
       app_gateway_ota_read_marker(GATEWAY_OTA_STAGE_CONSUMED_ADDRESS,
                                   &consumed) == HAL_OK)
    {
        if(complete == GATEWAY_OTA_MARKER_SET ||
           request == GATEWAY_OTA_MARKER_SET ||
           consumed == GATEWAY_OTA_MARKER_SET)
        {
            gateway_ota_status.expected_size =
                gateway_ota_manifest.image_size;
            gateway_ota_status.received_size =
                gateway_ota_manifest.image_size;
            gateway_ota_status.image_version =
                gateway_ota_manifest.image_version;
            gateway_ota_status.image_crc32 =
                gateway_ota_manifest.image_crc32;
            gateway_ota_status.install_requested =
                (request == GATEWAY_OTA_MARKER_SET) ? 1U : 0U;
            gateway_ota_status.consumed =
                (consumed == GATEWAY_OTA_MARKER_SET) ? 1U : 0U;

            if(consumed == GATEWAY_OTA_MARKER_SET)
            {
                gateway_ota_status.state = APP_GATEWAY_OTA_CONSUMED;
            }
            else if(request == GATEWAY_OTA_MARKER_SET)
            {
                gateway_ota_status.state = APP_GATEWAY_OTA_REQUESTED;
            }
            else
            {
                gateway_ota_status.state = APP_GATEWAY_OTA_VERIFYING;
                if(app_gateway_ota_verify_image() == HAL_OK)
                {
                    gateway_ota_status.state = APP_GATEWAY_OTA_READY;
                }
                else
                {
                    gateway_ota_status.state = APP_GATEWAY_OTA_ERROR;
                }
            }
        }
    }

    gateway_ota_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef app_gateway_ota_write_manifest(const uint8_t *manifest,
                                                  size_t length)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(gateway_ota_initialized == 0U || manifest == NULL ||
       length != sizeof(gateway_ota_manifest) ||
       app_gateway_ota_manifest_valid(
           (const gateway_ota_manifest_t *)manifest) == 0U)
    {
        return HAL_ERROR;
    }
    if(tx_mutex_get(&gateway_ota_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }

    memset(&gateway_ota_status, 0, sizeof(gateway_ota_status));
    gateway_ota_status.state = APP_GATEWAY_OTA_RECEIVING;
    memcpy(&gateway_ota_manifest, manifest, sizeof(gateway_ota_manifest));
    gateway_ota_status.expected_size = gateway_ota_manifest.image_size;
    gateway_ota_status.image_version = gateway_ota_manifest.image_version;
    gateway_ota_status.image_crc32 = gateway_ota_manifest.image_crc32;

    if(bsp_w25q128_erase_sector(GATEWAY_OTA_STAGE_ADDRESS) != HAL_OK ||
       bsp_w25q128_write(GATEWAY_OTA_STAGE_ADDRESS,
                         manifest,
                         length) != HAL_OK ||
       bsp_w25q128_read(GATEWAY_OTA_STAGE_ADDRESS,
                        gateway_ota_verify_buffer,
                        length) != HAL_OK ||
       memcmp(gateway_ota_verify_buffer, manifest, length) != 0)
    {
        gateway_ota_status.state = APP_GATEWAY_OTA_ERROR;
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_FLASH;
        goto done;
    }
    status = HAL_OK;

done:
    (void)tx_mutex_put(&gateway_ota_mutex);
    return status;
}

HAL_StatusTypeDef app_gateway_ota_write_image(uint32_t offset,
                                               const uint8_t *data,
                                               size_t length)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    size_t consumed = 0U;

    if(gateway_ota_initialized == 0U || data == NULL || length == 0U)
    {
        return HAL_ERROR;
    }
    if(tx_mutex_get(&gateway_ota_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if(gateway_ota_status.state != APP_GATEWAY_OTA_RECEIVING ||
       offset != gateway_ota_status.received_size)
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_SEQUENCE;
        goto done;
    }
    if(offset > gateway_ota_status.expected_size ||
       length > (size_t)(gateway_ota_status.expected_size - offset))
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_RANGE;
        goto done;
    }

    while(consumed < length)
    {
        uint32_t address = GATEWAY_OTA_STAGE_IMAGE_ADDRESS + offset +
                           (uint32_t)consumed;
        uint32_t sector_offset = address & (BSP_W25Q128_SECTOR_SIZE - 1U);
        size_t part = length - consumed;
        size_t verified = 0U;

        if(part > (size_t)(BSP_W25Q128_SECTOR_SIZE - sector_offset))
        {
            part = BSP_W25Q128_SECTOR_SIZE - sector_offset;
        }
        if(sector_offset == 0U && bsp_w25q128_erase_sector(address) != HAL_OK)
        {
            goto flash_error;
        }
        if(bsp_w25q128_write(address, &data[consumed], part) != HAL_OK)
        {
            goto flash_error;
        }
        while(verified < part)
        {
            size_t verify_length = part - verified;
            if(verify_length > sizeof(gateway_ota_verify_buffer))
            {
                verify_length = sizeof(gateway_ota_verify_buffer);
            }
            if(bsp_w25q128_read(address + (uint32_t)verified,
                                gateway_ota_verify_buffer,
                                verify_length) != HAL_OK ||
               memcmp(gateway_ota_verify_buffer,
                      &data[consumed + verified],
                      verify_length) != 0)
            {
                goto flash_error;
            }
            verified += verify_length;
        }
        consumed += part;
    }

    gateway_ota_status.received_size += (uint32_t)length;
    gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_NONE;
    status = HAL_OK;
    goto done;

flash_error:
    gateway_ota_status.state = APP_GATEWAY_OTA_ERROR;
    gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_FLASH;
done:
    (void)tx_mutex_put(&gateway_ota_mutex);
    return status;
}

HAL_StatusTypeDef app_gateway_ota_finish(void)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(gateway_ota_initialized == 0U ||
       tx_mutex_get(&gateway_ota_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if(gateway_ota_status.state == APP_GATEWAY_OTA_RECEIVING &&
       gateway_ota_status.received_size == gateway_ota_status.expected_size)
    {
        gateway_ota_status.state = APP_GATEWAY_OTA_VERIFYING;
        if(app_gateway_ota_verify_image() == HAL_OK &&
           app_gateway_ota_set_marker(
               GATEWAY_OTA_STAGE_COMPLETE_ADDRESS) == HAL_OK)
        {
            gateway_ota_status.state = APP_GATEWAY_OTA_READY;
            status = HAL_OK;
        }
        else
        {
            gateway_ota_status.state = APP_GATEWAY_OTA_ERROR;
        }
    }
    else
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_STATE;
    }
    (void)tx_mutex_put(&gateway_ota_mutex);
    return status;
}

HAL_StatusTypeDef app_gateway_ota_request_install(void)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(gateway_ota_initialized == 0U ||
       tx_mutex_get(&gateway_ota_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if(gateway_ota_status.state == APP_GATEWAY_OTA_READY &&
       app_gateway_ota_set_marker(GATEWAY_OTA_STAGE_REQUEST_ADDRESS) == HAL_OK)
    {
        gateway_ota_status.state = APP_GATEWAY_OTA_REQUESTED;
        gateway_ota_status.install_requested = 1U;
        status = HAL_OK;
    }
    else
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_STATE;
    }
    (void)tx_mutex_put(&gateway_ota_mutex);
    return status;
}

void app_gateway_ota_get_status(app_gateway_ota_status_t *status)
{
    if(status == NULL)
    {
        return;
    }
    if(gateway_ota_initialized != 0U &&
       tx_mutex_get(&gateway_ota_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        *status = gateway_ota_status;
        status->boot_error = RTC->BKP7R;
        status->active_version = RTC->BKP8R;
        status->pending_version = RTC->BKP9R;
        status->boot_sequence = RTC->BKP10R;
        (void)tx_mutex_put(&gateway_ota_mutex);
    }
    else
    {
        memset(status, 0, sizeof(*status));
    }
}

const char *app_gateway_ota_state_name(uint8_t state)
{
    static const char *const names[] =
    {
        "empty", "receiving", "verifying", "ready",
        "requested", "consumed", "error"
    };
    return (state < (sizeof(names) / sizeof(names[0]))) ?
           names[state] : "unknown";
}

static uint8_t app_gateway_ota_manifest_valid(
    const gateway_ota_manifest_t *manifest)
{
    gateway_ota_manifest_t copy;
    uint32_t crc;

    if(manifest == NULL)
    {
        return 0U;
    }
    copy = *manifest;
    copy.manifest_crc32 = 0U;
    crc = ota_modbus_crc32((const uint8_t *)&copy, sizeof(copy));
    if(manifest->magic != GATEWAY_OTA_MANIFEST_MAGIC ||
       manifest->version != GATEWAY_OTA_MANIFEST_SCHEMA ||
       manifest->manifest_crc32 != crc ||
       manifest->image_size == 0U ||
       manifest->image_size > GATEWAY_OTA_STAGE_IMAGE_CAPACITY ||
       manifest->package_address != GATEWAY_OTA_STAGE_IMAGE_ADDRESS ||
       manifest->package_size != manifest->image_size ||
       manifest->load_address != GATEWAY_OTA_LOAD_ADDRESS ||
       manifest->entry_address < GATEWAY_OTA_LOAD_ADDRESS ||
       manifest->entry_address >=
           (GATEWAY_OTA_LOAD_ADDRESS + GATEWAY_OTA_EXEC_SIZE) ||
       (manifest->entry_address & 1U) == 0U ||
       (manifest->image_flags & GATEWAY_OTA_IMAGE_FLAG_SIGNED) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static HAL_StatusTypeDef app_gateway_ota_verify_image(void)
{
    ota_sha256_context_t sha256;
    uint8_t digest[32];
    uint32_t crc = 0U;
    uint32_t offset = 0U;

    ota_sha256_init(&sha256);
    while(offset < gateway_ota_status.expected_size)
    {
        uint32_t length = gateway_ota_status.expected_size - offset;
        if(length > sizeof(gateway_ota_verify_buffer))
        {
            length = sizeof(gateway_ota_verify_buffer);
        }
        if(bsp_w25q128_read(GATEWAY_OTA_STAGE_IMAGE_ADDRESS + offset,
                            gateway_ota_verify_buffer,
                            length) != HAL_OK)
        {
            gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_FLASH;
            return HAL_ERROR;
        }
        crc = ota_modbus_crc32_update(crc,
                                      gateway_ota_verify_buffer,
                                      length);
        ota_sha256_update(&sha256, gateway_ota_verify_buffer, length);
        offset += length;
    }
    ota_sha256_finish(&sha256, digest);
    if(crc != gateway_ota_manifest.image_crc32)
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_CRC;
        memset(digest, 0, sizeof(digest));
        return HAL_ERROR;
    }
    if(memcmp(digest,
              gateway_ota_manifest.image_sha256,
              sizeof(digest)) != 0 ||
       memcmp(digest,
              gateway_ota_manifest.package_sha256,
              sizeof(digest)) != 0)
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_SHA256;
        memset(digest, 0, sizeof(digest));
        return HAL_ERROR;
    }
    memset(digest, 0, sizeof(digest));
    gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_NONE;
    return HAL_OK;
}

static HAL_StatusTypeDef app_gateway_ota_read_marker(uint32_t address,
                                                      uint32_t *marker)
{
    if(marker == NULL)
    {
        return HAL_ERROR;
    }
    return bsp_w25q128_read(address, (uint8_t *)marker, sizeof(*marker));
}

static HAL_StatusTypeDef app_gateway_ota_set_marker(uint32_t address)
{
    uint32_t marker = GATEWAY_OTA_MARKER_SET;
    uint32_t verify = GATEWAY_OTA_MARKER_ERASED;

    if(bsp_w25q128_write(address,
                         (const uint8_t *)&marker,
                         sizeof(marker)) != HAL_OK ||
       bsp_w25q128_read(address,
                        (uint8_t *)&verify,
                        sizeof(verify)) != HAL_OK ||
       verify != GATEWAY_OTA_MARKER_SET)
    {
        gateway_ota_status.last_error = APP_GATEWAY_OTA_ERROR_FLASH;
        return HAL_ERROR;
    }
    return HAL_OK;
}
