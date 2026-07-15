#include "app_device_config.h"

#include <stddef.h>
#include <string.h>

#include "bsp.h"
#include "bsp_flash_layout.h"

#define APP_DEVICE_CONFIG_MAGIC      0x31474643UL
#define APP_DEVICE_CONFIG_VERSION_V1 1U
#define APP_DEVICE_CONFIG_VERSION    2U

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint16_t version;
    uint16_t length;
} app_device_config_record_header_t;

typedef struct
{
    app_device_config_record_header_t header;
    uint8_t modbus_unit_id;
    uint8_t reserved[3];
    uint32_t crc32;
} app_device_config_record_v1_t;

typedef struct
{
    app_device_config_record_header_t header;
    app_device_config_t config;
    uint32_t crc32;
} app_device_config_record_t;

static uint32_t app_device_config_crc32(const uint8_t *data, size_t length);
static uint8_t app_device_config_record_is_valid(
    const app_device_config_record_t *record);
static HAL_StatusTypeDef app_device_config_read_records(
    app_device_config_record_t *slot_a,
    app_device_config_record_t *slot_b);
static const app_device_config_record_t *app_device_config_newest(
    const app_device_config_record_t *slot_a,
    const app_device_config_record_t *slot_b,
    uint8_t slot_a_valid,
    uint8_t slot_b_valid);
static void app_device_config_extract(
    const app_device_config_record_t *record,
    app_device_config_t *config);
static void app_device_config_normalize(app_device_config_t *config);

void app_device_config_set_defaults(app_device_config_t *config)
{
    uint32_t device_index;
    uint32_t class_index;

    if (config == NULL)
    {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->rs485_role = APP_RS485_ROLE_SLAVE;
    config->modbus_unit_id = APP_DEVICE_CONFIG_DEFAULT_MODBUS_UNIT_ID;
    config->master_device_count = 1U;
    config->offline_probe_period_s = 60U;
    config->poll_period_ms = APP_DEVICE_CONFIG_DEFAULT_POLL_PERIOD_MS;

    for (device_index = 0U;
         device_index < APP_DEVICE_CONFIG_MAX_MASTER_DEVICES;
         device_index++)
    {
        app_modbus_master_device_config_t *device =
            &config->master_devices[device_index];

        device->unit_id = (uint8_t)(device_index + 1U);
        device->response_timeout_ms =
            APP_DEVICE_CONFIG_DEFAULT_RESPONSE_TIMEOUT_MS;
        for (class_index = 0U;
             class_index < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
             class_index++)
        {
            device->ranges[class_index].address = 0U;
            device->ranges[class_index].quantity = 4U;
        }
    }
}

uint8_t app_device_config_validate(const app_device_config_t *config)
{
    uint32_t device_index;

    if ((config == NULL) ||
        (config->rs485_role > APP_RS485_ROLE_MASTER) ||
        (config->modbus_unit_id == 0U) ||
        (config->modbus_unit_id > 247U) ||
        (config->master_device_count == 0U) ||
        (config->master_device_count >
         APP_DEVICE_CONFIG_MAX_MASTER_DEVICES) ||
        ((config->offline_probe_period_s != 60U) &&
         (config->offline_probe_period_s != 300U)) ||
        (config->poll_period_ms < APP_DEVICE_CONFIG_MIN_POLL_PERIOD_MS) ||
        (config->poll_period_ms > APP_DEVICE_CONFIG_MAX_POLL_PERIOD_MS))
    {
        return 0U;
    }

    for (device_index = 0U;
         device_index < config->master_device_count;
         device_index++)
    {
        const app_modbus_master_device_config_t *device =
            &config->master_devices[device_index];
        uint32_t class_index;
        uint8_t enabled_ranges = 0U;
        uint32_t other_index;

        if ((device->unit_id == 0U) ||
            (device->unit_id > 247U) ||
            (device->response_timeout_ms <
             APP_DEVICE_CONFIG_MIN_RESPONSE_TIMEOUT_MS) ||
            (device->response_timeout_ms >
             APP_DEVICE_CONFIG_MAX_RESPONSE_TIMEOUT_MS))
        {
            return 0U;
        }

        for (other_index = 0U; other_index < device_index; other_index++)
        {
            if (config->master_devices[other_index].unit_id ==
                device->unit_id)
            {
                return 0U;
            }
        }

        for (class_index = 0U;
             class_index < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
             class_index++)
        {
            const app_modbus_poll_range_t *range =
                &device->ranges[class_index];

            if (range->quantity > APP_DEVICE_CONFIG_MAX_RANGE_QUANTITY)
            {
                return 0U;
            }
            if ((range->quantity != 0U) &&
                (((uint32_t)range->address + range->quantity) > 65536UL))
            {
                return 0U;
            }
            if (range->quantity != 0U)
            {
                enabled_ranges++;
            }
        }
        if (enabled_ranges == 0U)
        {
            return 0U;
        }
    }

    return 1U;
}

HAL_StatusTypeDef app_device_config_load(app_device_config_t *config,
                                         uint8_t *loaded_from_flash)
{
    app_device_config_record_t slot_a;
    app_device_config_record_t slot_b;
    const app_device_config_record_t *newest;
    uint8_t slot_a_valid;
    uint8_t slot_b_valid;

    if ((config == NULL) || (loaded_from_flash == NULL))
    {
        return HAL_ERROR;
    }

    app_device_config_set_defaults(config);
    *loaded_from_flash = 0U;

    if (app_device_config_read_records(&slot_a, &slot_b) != HAL_OK)
    {
        return HAL_ERROR;
    }

    slot_a_valid = app_device_config_record_is_valid(&slot_a);
    slot_b_valid = app_device_config_record_is_valid(&slot_b);
    newest = app_device_config_newest(&slot_a,
                                      &slot_b,
                                      slot_a_valid,
                                      slot_b_valid);
    if (newest != NULL)
    {
        app_device_config_extract(newest, config);
        *loaded_from_flash = 1U;
    }

    return HAL_OK;
}

HAL_StatusTypeDef app_device_config_save(const app_device_config_t *config)
{
    app_device_config_record_t slot_a;
    app_device_config_record_t slot_b;
    app_device_config_record_t record;
    app_device_config_record_t verification;
    const app_device_config_record_t *newest;
    app_device_config_t normalized;
    app_device_config_t newest_config;
    uint32_t target_address;
    uint32_t next_sequence = 1UL;
    uint8_t slot_a_valid;
    uint8_t slot_b_valid;

    if ((config == NULL) || (app_device_config_validate(config) == 0U))
    {
        return HAL_ERROR;
    }

    normalized = *config;
    app_device_config_normalize(&normalized);
    if (app_device_config_read_records(&slot_a, &slot_b) != HAL_OK)
    {
        return HAL_ERROR;
    }

    slot_a_valid = app_device_config_record_is_valid(&slot_a);
    slot_b_valid = app_device_config_record_is_valid(&slot_b);
    newest = app_device_config_newest(&slot_a,
                                      &slot_b,
                                      slot_a_valid,
                                      slot_b_valid);
    if (newest != NULL)
    {
        app_device_config_extract(newest, &newest_config);
        app_device_config_normalize(&newest_config);
        if (memcmp(&newest_config, &normalized, sizeof(normalized)) == 0)
        {
            return HAL_OK;
        }
    }

    if (newest == &slot_a)
    {
        target_address = BSP_FLASH_CONFIG_SLOT_B_ADDRESS;
        next_sequence = slot_a.header.sequence + 1UL;
    }
    else if (newest == &slot_b)
    {
        target_address = BSP_FLASH_CONFIG_SLOT_A_ADDRESS;
        next_sequence = slot_b.header.sequence + 1UL;
    }
    else
    {
        target_address = BSP_FLASH_CONFIG_SLOT_A_ADDRESS;
    }
    if (next_sequence == 0UL)
    {
        next_sequence = 1UL;
    }

    memset(&record, 0, sizeof(record));
    record.header.magic = APP_DEVICE_CONFIG_MAGIC;
    record.header.sequence = next_sequence;
    record.header.version = APP_DEVICE_CONFIG_VERSION;
    record.header.length = (uint16_t)sizeof(record);
    record.config = normalized;
    record.crc32 = app_device_config_crc32(
        (const uint8_t *)&record,
        offsetof(app_device_config_record_t, crc32));

    if (bsp_w25q128_erase_sector(target_address) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (bsp_w25q128_write(target_address,
                          (const uint8_t *)&record,
                          sizeof(record)) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (bsp_w25q128_read(target_address,
                         (uint8_t *)&verification,
                         sizeof(verification)) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if ((app_device_config_record_is_valid(&verification) == 0U) ||
        (verification.header.sequence != record.header.sequence) ||
        (memcmp(&verification.config,
                &record.config,
                sizeof(record.config)) != 0))
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef app_device_config_get_diagnostics(
    app_device_config_diagnostics_t *diagnostics)
{
    app_device_config_record_t slot_a;
    app_device_config_record_t slot_b;
    app_device_config_t config;

    if (diagnostics == NULL)
    {
        return HAL_ERROR;
    }
    if (app_device_config_read_records(&slot_a, &slot_b) != HAL_OK)
    {
        return HAL_ERROR;
    }

    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->slot_a_sequence = slot_a.header.sequence;
    diagnostics->slot_b_sequence = slot_b.header.sequence;
    diagnostics->slot_a_version = slot_a.header.version;
    diagnostics->slot_b_version = slot_b.header.version;
    diagnostics->slot_a_valid = app_device_config_record_is_valid(&slot_a);
    diagnostics->slot_b_valid = app_device_config_record_is_valid(&slot_b);

    if (diagnostics->slot_a_valid != 0U)
    {
        app_device_config_extract(&slot_a, &config);
        diagnostics->slot_a_modbus_unit_id = config.modbus_unit_id;
        diagnostics->slot_a_rs485_role = config.rs485_role;
        diagnostics->slot_a_master_device_count =
            config.master_device_count;
    }
    if (diagnostics->slot_b_valid != 0U)
    {
        app_device_config_extract(&slot_b, &config);
        diagnostics->slot_b_modbus_unit_id = config.modbus_unit_id;
        diagnostics->slot_b_rs485_role = config.rs485_role;
        diagnostics->slot_b_master_device_count =
            config.master_device_count;
    }
    return HAL_OK;
}

static uint32_t app_device_config_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;

    for (index = 0U; index < length; index++)
    {
        uint32_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = 0UL - (crc & 1UL);

            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static uint8_t app_device_config_record_is_valid(
    const app_device_config_record_t *record)
{
    if (record->header.magic != APP_DEVICE_CONFIG_MAGIC)
    {
        return 0U;
    }

    if ((record->header.version == APP_DEVICE_CONFIG_VERSION_V1) &&
        (record->header.length == sizeof(app_device_config_record_v1_t)))
    {
        const app_device_config_record_v1_t *legacy =
            (const app_device_config_record_v1_t *)record;
        uint32_t expected_crc = app_device_config_crc32(
            (const uint8_t *)legacy,
            offsetof(app_device_config_record_v1_t, crc32));

        return ((legacy->modbus_unit_id != 0U) &&
                (legacy->modbus_unit_id <= 247U) &&
                (legacy->crc32 == expected_crc)) ? 1U : 0U;
    }

    if ((record->header.version == APP_DEVICE_CONFIG_VERSION) &&
        (record->header.length == sizeof(*record)))
    {
        uint32_t expected_crc = app_device_config_crc32(
            (const uint8_t *)record,
            offsetof(app_device_config_record_t, crc32));

        return ((record->crc32 == expected_crc) &&
                (app_device_config_validate(&record->config) != 0U)) ?
               1U : 0U;
    }

    return 0U;
}

static HAL_StatusTypeDef app_device_config_read_records(
    app_device_config_record_t *slot_a,
    app_device_config_record_t *slot_b)
{
    memset(slot_a, 0xFF, sizeof(*slot_a));
    memset(slot_b, 0xFF, sizeof(*slot_b));
    if (bsp_w25q128_read(BSP_FLASH_CONFIG_SLOT_A_ADDRESS,
                         (uint8_t *)slot_a,
                         sizeof(*slot_a)) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (bsp_w25q128_read(BSP_FLASH_CONFIG_SLOT_B_ADDRESS,
                         (uint8_t *)slot_b,
                         sizeof(*slot_b)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static const app_device_config_record_t *app_device_config_newest(
    const app_device_config_record_t *slot_a,
    const app_device_config_record_t *slot_b,
    uint8_t slot_a_valid,
    uint8_t slot_b_valid)
{
    if ((slot_a_valid != 0U) && (slot_b_valid != 0U))
    {
        return ((int32_t)(slot_a->header.sequence -
                          slot_b->header.sequence) > 0) ?
               slot_a : slot_b;
    }
    if (slot_a_valid != 0U)
    {
        return slot_a;
    }
    if (slot_b_valid != 0U)
    {
        return slot_b;
    }

    return NULL;
}

static void app_device_config_extract(
    const app_device_config_record_t *record,
    app_device_config_t *config)
{
    app_device_config_set_defaults(config);
    if (record->header.version == APP_DEVICE_CONFIG_VERSION_V1)
    {
        const app_device_config_record_v1_t *legacy =
            (const app_device_config_record_v1_t *)record;

        config->modbus_unit_id = legacy->modbus_unit_id;
    }
    else
    {
        *config = record->config;
        app_device_config_normalize(config);
    }
}

static void app_device_config_normalize(app_device_config_t *config)
{
    uint32_t device_index;

    config->reserved = 0U;
    for (device_index = 0U;
         device_index < APP_DEVICE_CONFIG_MAX_MASTER_DEVICES;
         device_index++)
    {
        config->master_devices[device_index].reserved = 0U;
    }
}
