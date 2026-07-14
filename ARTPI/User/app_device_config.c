#include "app_device_config.h"

#include <stddef.h>
#include <string.h>

#include "bsp.h"
#include "bsp_flash_layout.h"

#define APP_DEVICE_CONFIG_MAGIC   0x31474643UL
#define APP_DEVICE_CONFIG_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint16_t version;
    uint16_t length;
    uint8_t modbus_unit_id;
    uint8_t reserved[3];
    uint32_t crc32;
} app_device_config_record_t;

static uint32_t app_device_config_crc32(const uint8_t *data, size_t length);
static uint8_t app_device_config_is_valid(
    const app_device_config_record_t *record);
static HAL_StatusTypeDef app_device_config_read_records(
    app_device_config_record_t *slot_a,
    app_device_config_record_t *slot_b);
static const app_device_config_record_t *app_device_config_newest(
    const app_device_config_record_t *slot_a,
    const app_device_config_record_t *slot_b,
    uint8_t slot_a_valid,
    uint8_t slot_b_valid);

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

    config->modbus_unit_id = APP_DEVICE_CONFIG_DEFAULT_MODBUS_UNIT_ID;
    *loaded_from_flash = 0U;

    if (app_device_config_read_records(&slot_a, &slot_b) != HAL_OK)
    {
        return HAL_ERROR;
    }

    slot_a_valid = app_device_config_is_valid(&slot_a);
    slot_b_valid = app_device_config_is_valid(&slot_b);
    newest = app_device_config_newest(&slot_a,
                                      &slot_b,
                                      slot_a_valid,
                                      slot_b_valid);
    if (newest != NULL)
    {
        config->modbus_unit_id = newest->modbus_unit_id;
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
    uint32_t target_address;
    uint32_t next_sequence = 1UL;
    uint8_t slot_a_valid;
    uint8_t slot_b_valid;

    if ((config == NULL) ||
        (config->modbus_unit_id == 0U) ||
        (config->modbus_unit_id > 247U))
    {
        return HAL_ERROR;
    }

    if (app_device_config_read_records(&slot_a, &slot_b) != HAL_OK)
    {
        return HAL_ERROR;
    }

    slot_a_valid = app_device_config_is_valid(&slot_a);
    slot_b_valid = app_device_config_is_valid(&slot_b);
    newest = app_device_config_newest(&slot_a,
                                      &slot_b,
                                      slot_a_valid,
                                      slot_b_valid);
    if ((newest != NULL) &&
        (newest->modbus_unit_id == config->modbus_unit_id))
    {
        return HAL_OK;
    }

    if (newest == &slot_a)
    {
        target_address = BSP_FLASH_CONFIG_SLOT_B_ADDRESS;
        next_sequence = slot_a.sequence + 1UL;
    }
    else if (newest == &slot_b)
    {
        target_address = BSP_FLASH_CONFIG_SLOT_A_ADDRESS;
        next_sequence = slot_b.sequence + 1UL;
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
    record.magic = APP_DEVICE_CONFIG_MAGIC;
    record.sequence = next_sequence;
    record.version = APP_DEVICE_CONFIG_VERSION;
    record.length = (uint16_t)sizeof(record);
    record.modbus_unit_id = config->modbus_unit_id;
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
    if ((app_device_config_is_valid(&verification) == 0U) ||
        (verification.sequence != record.sequence) ||
        (verification.modbus_unit_id != record.modbus_unit_id))
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

    if (diagnostics == NULL)
    {
        return HAL_ERROR;
    }
    if (app_device_config_read_records(&slot_a, &slot_b) != HAL_OK)
    {
        return HAL_ERROR;
    }

    diagnostics->slot_a_sequence = slot_a.sequence;
    diagnostics->slot_b_sequence = slot_b.sequence;
    diagnostics->slot_a_valid = app_device_config_is_valid(&slot_a);
    diagnostics->slot_b_valid = app_device_config_is_valid(&slot_b);
    diagnostics->slot_a_modbus_unit_id = slot_a.modbus_unit_id;
    diagnostics->slot_b_modbus_unit_id = slot_b.modbus_unit_id;
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

static uint8_t app_device_config_is_valid(
    const app_device_config_record_t *record)
{
    uint32_t expected_crc;

    if ((record->magic != APP_DEVICE_CONFIG_MAGIC) ||
        (record->version != APP_DEVICE_CONFIG_VERSION) ||
        (record->length != sizeof(*record)) ||
        (record->modbus_unit_id == 0U) ||
        (record->modbus_unit_id > 247U))
    {
        return 0U;
    }

    expected_crc = app_device_config_crc32(
        (const uint8_t *)record,
        offsetof(app_device_config_record_t, crc32));
    return (record->crc32 == expected_crc) ? 1U : 0U;
}

static HAL_StatusTypeDef app_device_config_read_records(
    app_device_config_record_t *slot_a,
    app_device_config_record_t *slot_b)
{
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
        return ((int32_t)(slot_a->sequence - slot_b->sequence) > 0) ?
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
