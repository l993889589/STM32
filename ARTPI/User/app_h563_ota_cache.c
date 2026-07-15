#include "app_h563_ota_cache.h"

#include <string.h>

#include "bsp.h"
#include "tx_api.h"

#define APP_H563_OTA_MANIFEST_MAGIC        0x4F54414DUL
#define APP_H563_OTA_MANIFEST_VERSION      1UL
#define APP_H563_OTA_IMAGE_ADDRESS         \
    (BSP_FLASH_DOWNLOAD_ADDRESS + BSP_W25Q128_SECTOR_SIZE)
#define APP_H563_OTA_IMAGE_CAPACITY        \
    (BSP_FLASH_DOWNLOAD_SIZE - BSP_W25Q128_SECTOR_SIZE)
#define APP_H563_OTA_VERIFY_BUFFER_SIZE     1024U

typedef enum
{
    APP_H563_OTA_CACHE_ERROR_NONE = 0,
    APP_H563_OTA_CACHE_ERROR_MANIFEST = 1,
    APP_H563_OTA_CACHE_ERROR_RANGE = 2,
    APP_H563_OTA_CACHE_ERROR_SEQUENCE = 3,
    APP_H563_OTA_CACHE_ERROR_FLASH = 4,
    APP_H563_OTA_CACHE_ERROR_CRC = 5,
    APP_H563_OTA_CACHE_ERROR_BUSY = 6
} app_h563_ota_cache_error_t;

static TX_MUTEX app_h563_ota_cache_mutex;
static uint8_t app_h563_ota_manifest[APP_H563_OTA_MANIFEST_SIZE];
static uint8_t app_h563_ota_verify_buffer[APP_H563_OTA_VERIFY_BUFFER_SIZE];
static app_h563_ota_cache_status_t app_h563_ota_cache_status;
static uint8_t app_h563_ota_cache_initialized;

static uint32_t app_h563_ota_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint8_t app_h563_ota_manifest_is_valid(const uint8_t *manifest)
{
    uint8_t manifest_copy[APP_H563_OTA_MANIFEST_SIZE];
    uint32_t image_size;
    uint32_t manifest_crc32;

    memcpy(manifest_copy, manifest, sizeof(manifest_copy));
    memset(&manifest_copy[APP_H563_OTA_MANIFEST_SIZE - sizeof(uint32_t)],
           0,
           sizeof(uint32_t));
    manifest_crc32 = ota_modbus_crc32(manifest_copy, sizeof(manifest_copy));

    if(app_h563_ota_get_u32(&manifest[0]) != APP_H563_OTA_MANIFEST_MAGIC ||
       app_h563_ota_get_u32(&manifest[4]) != APP_H563_OTA_MANIFEST_VERSION ||
       manifest_crc32 != app_h563_ota_get_u32(&manifest[184]))
    {
        return 0U;
    }

    image_size = app_h563_ota_get_u32(&manifest[12]);
    if(image_size == 0U || image_size > APP_H563_OTA_IMAGE_CAPACITY ||
       app_h563_ota_get_u32(&manifest[32]) != image_size ||
       app_h563_ota_get_u32(&manifest[48]) != 0x08020000UL ||
       (app_h563_ota_get_u32(&manifest[52]) & 1U) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static HAL_StatusTypeDef app_h563_ota_verify_image(void)
{
    uint32_t crc = 0U;
    uint32_t offset = 0U;

    while(offset < app_h563_ota_cache_status.expected_size)
    {
        uint32_t length = app_h563_ota_cache_status.expected_size - offset;

        if(length > sizeof(app_h563_ota_verify_buffer))
            length = sizeof(app_h563_ota_verify_buffer);
        if(bsp_w25q128_read(APP_H563_OTA_IMAGE_ADDRESS + offset,
                            app_h563_ota_verify_buffer,
                            length) != HAL_OK)
        {
            app_h563_ota_cache_status.last_error =
                APP_H563_OTA_CACHE_ERROR_FLASH;
            return HAL_ERROR;
        }
        crc = ota_modbus_crc32_update(crc,
                                      app_h563_ota_verify_buffer,
                                      length);
        offset += length;
    }

    if(crc != app_h563_ota_cache_status.image_crc32)
    {
        app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_CRC;
        return HAL_ERROR;
    }
    return HAL_OK;
}

HAL_StatusTypeDef app_h563_ota_cache_init(void)
{
    if(app_h563_ota_cache_initialized != 0U)
        return HAL_OK;
    if(tx_mutex_create(&app_h563_ota_cache_mutex,
                       "h563_ota_cache",
                       TX_INHERIT) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }

    memset(&app_h563_ota_cache_status, 0, sizeof(app_h563_ota_cache_status));
    if(bsp_w25q128_read(BSP_FLASH_DOWNLOAD_ADDRESS,
                        app_h563_ota_manifest,
                        sizeof(app_h563_ota_manifest)) == HAL_OK &&
       app_h563_ota_manifest_is_valid(app_h563_ota_manifest) != 0U)
    {
        app_h563_ota_cache_status.expected_size =
            app_h563_ota_get_u32(&app_h563_ota_manifest[12]);
        app_h563_ota_cache_status.received_size =
            app_h563_ota_cache_status.expected_size;
        app_h563_ota_cache_status.image_version =
            app_h563_ota_get_u32(&app_h563_ota_manifest[16]);
        app_h563_ota_cache_status.image_crc32 =
            app_h563_ota_get_u32(&app_h563_ota_manifest[24]);
        app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_VERIFYING;
        if(app_h563_ota_verify_image() == HAL_OK)
            app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_READY;
        else
            app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_ERROR;
    }

    app_h563_ota_cache_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef app_h563_ota_cache_write_manifest(const uint8_t *manifest,
                                                     size_t length)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(app_h563_ota_cache_initialized == 0U || manifest == NULL ||
       length != APP_H563_OTA_MANIFEST_SIZE ||
       app_h563_ota_manifest_is_valid(manifest) == 0U)
    {
        return HAL_ERROR;
    }
    if(tx_mutex_get(&app_h563_ota_cache_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        return HAL_ERROR;

    if(app_h563_ota_cache_status.state == APP_H563_OTA_CACHE_TRANSFERRING)
    {
        app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_BUSY;
        goto done;
    }
    app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_RECEIVING;
    app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_NONE;
    app_h563_ota_cache_status.received_size = 0U;
    app_h563_ota_cache_status.expected_size = app_h563_ota_get_u32(&manifest[12]);
    app_h563_ota_cache_status.image_version = app_h563_ota_get_u32(&manifest[16]);
    app_h563_ota_cache_status.image_crc32 = app_h563_ota_get_u32(&manifest[24]);
    memcpy(app_h563_ota_manifest, manifest, length);

    if(bsp_w25q128_erase_sector(BSP_FLASH_DOWNLOAD_ADDRESS) != HAL_OK ||
       bsp_w25q128_write(BSP_FLASH_DOWNLOAD_ADDRESS,
                         manifest,
                         length) != HAL_OK ||
       bsp_w25q128_read(BSP_FLASH_DOWNLOAD_ADDRESS,
                        app_h563_ota_verify_buffer,
                        length) != HAL_OK ||
       memcmp(app_h563_ota_verify_buffer, manifest, length) != 0)
    {
        app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_ERROR;
        app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_FLASH;
        goto done;
    }
    status = HAL_OK;

done:
    (void)tx_mutex_put(&app_h563_ota_cache_mutex);
    return status;
}

HAL_StatusTypeDef app_h563_ota_cache_write_image(uint32_t offset,
                                                  const uint8_t *data,
                                                  size_t length)
{
    HAL_StatusTypeDef status = HAL_ERROR;
    size_t consumed = 0U;

    if(app_h563_ota_cache_initialized == 0U || data == NULL || length == 0U)
        return HAL_ERROR;
    if(tx_mutex_get(&app_h563_ota_cache_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        return HAL_ERROR;

    if(app_h563_ota_cache_status.state != APP_H563_OTA_CACHE_RECEIVING ||
       offset != app_h563_ota_cache_status.received_size)
    {
        app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_SEQUENCE;
        goto done;
    }
    if(offset > app_h563_ota_cache_status.expected_size ||
       length > (size_t)(app_h563_ota_cache_status.expected_size - offset))
    {
        app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_RANGE;
        goto done;
    }

    while(consumed < length)
    {
        uint32_t address = APP_H563_OTA_IMAGE_ADDRESS + offset + (uint32_t)consumed;
        uint32_t sector_offset = address & (BSP_W25Q128_SECTOR_SIZE - 1U);
        size_t part = length - consumed;

        if(part > (size_t)(BSP_W25Q128_SECTOR_SIZE - sector_offset))
            part = BSP_W25Q128_SECTOR_SIZE - sector_offset;
        if(sector_offset == 0U && bsp_w25q128_erase_sector(address) != HAL_OK)
            goto flash_error;
        if(bsp_w25q128_write(address, &data[consumed], part) != HAL_OK)
            goto flash_error;

        {
            size_t verified = 0U;
            while(verified < part)
            {
                size_t verify_length = part - verified;
                if(verify_length > sizeof(app_h563_ota_verify_buffer))
                    verify_length = sizeof(app_h563_ota_verify_buffer);
                if(bsp_w25q128_read(address + (uint32_t)verified,
                                    app_h563_ota_verify_buffer,
                                    verify_length) != HAL_OK ||
                   memcmp(app_h563_ota_verify_buffer,
                          &data[consumed + verified],
                          verify_length) != 0)
                {
                    goto flash_error;
                }
                verified += verify_length;
            }
        }
        consumed += part;
    }

    app_h563_ota_cache_status.received_size += (uint32_t)length;
    app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_NONE;
    status = HAL_OK;
    goto done;

flash_error:
    app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_ERROR;
    app_h563_ota_cache_status.last_error = APP_H563_OTA_CACHE_ERROR_FLASH;
done:
    (void)tx_mutex_put(&app_h563_ota_cache_mutex);
    return status;
}

HAL_StatusTypeDef app_h563_ota_cache_finish(void)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(app_h563_ota_cache_initialized == 0U ||
       tx_mutex_get(&app_h563_ota_cache_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if(app_h563_ota_cache_status.state == APP_H563_OTA_CACHE_RECEIVING &&
       app_h563_ota_cache_status.received_size ==
           app_h563_ota_cache_status.expected_size)
    {
        app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_VERIFYING;
        if(app_h563_ota_verify_image() == HAL_OK)
        {
            app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_READY;
            status = HAL_OK;
        }
        else
        {
            app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_ERROR;
        }
    }
    (void)tx_mutex_put(&app_h563_ota_cache_mutex);
    return status;
}

HAL_StatusTypeDef app_h563_ota_cache_get_descriptor(
    ota_modbus_descriptor_t *descriptor)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(descriptor == NULL || app_h563_ota_cache_initialized == 0U ||
       tx_mutex_get(&app_h563_ota_cache_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if(app_h563_ota_cache_status.state == APP_H563_OTA_CACHE_READY ||
       app_h563_ota_cache_status.state == APP_H563_OTA_CACHE_TRANSFERRING)
    {
        memset(descriptor, 0, sizeof(*descriptor));
        descriptor->state = 2U;
        descriptor->image_version = app_h563_ota_get_u32(&app_h563_ota_manifest[16]);
        descriptor->image_size = app_h563_ota_get_u32(&app_h563_ota_manifest[12]);
        descriptor->image_crc32 = app_h563_ota_get_u32(&app_h563_ota_manifest[24]);
        descriptor->image_flags = app_h563_ota_get_u32(&app_h563_ota_manifest[20]);
        descriptor->load_address = app_h563_ota_get_u32(&app_h563_ota_manifest[48]);
        descriptor->entry_address = app_h563_ota_get_u32(&app_h563_ota_manifest[52]);
        memcpy(descriptor->image_sha256, &app_h563_ota_manifest[56], 32U);
        memcpy(descriptor->signature, &app_h563_ota_manifest[120], 64U);
        status = HAL_OK;
    }
    (void)tx_mutex_put(&app_h563_ota_cache_mutex);
    return status;
}

HAL_StatusTypeDef app_h563_ota_cache_read_image(uint32_t offset,
                                                 uint8_t *data,
                                                 size_t length)
{
    if(data == NULL || app_h563_ota_cache_initialized == 0U ||
       offset > app_h563_ota_cache_status.expected_size ||
       length > (size_t)(app_h563_ota_cache_status.expected_size - offset))
    {
        return HAL_ERROR;
    }
    return bsp_w25q128_read(APP_H563_OTA_IMAGE_ADDRESS + offset, data, length);
}

HAL_StatusTypeDef app_h563_ota_cache_set_transferring(uint8_t active)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if(app_h563_ota_cache_initialized == 0U ||
       tx_mutex_get(&app_h563_ota_cache_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return HAL_ERROR;
    }
    if(active != 0U && app_h563_ota_cache_status.state == APP_H563_OTA_CACHE_READY)
    {
        app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_TRANSFERRING;
        status = HAL_OK;
    }
    else if(active == 0U &&
            app_h563_ota_cache_status.state == APP_H563_OTA_CACHE_TRANSFERRING)
    {
        app_h563_ota_cache_status.state = APP_H563_OTA_CACHE_READY;
        status = HAL_OK;
    }
    (void)tx_mutex_put(&app_h563_ota_cache_mutex);
    return status;
}

void app_h563_ota_cache_get_status(app_h563_ota_cache_status_t *status)
{
    if(status == NULL)
        return;
    if(app_h563_ota_cache_initialized != 0U &&
       tx_mutex_get(&app_h563_ota_cache_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
        *status = app_h563_ota_cache_status;
        (void)tx_mutex_put(&app_h563_ota_cache_mutex);
    }
    else
    {
        memset(status, 0, sizeof(*status));
    }
}

const char *app_h563_ota_cache_state_name(uint8_t state)
{
    static const char *const names[] =
    {
        "empty", "receiving", "verifying", "ready", "transferring", "error"
    };
    return (state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "unknown";
}
