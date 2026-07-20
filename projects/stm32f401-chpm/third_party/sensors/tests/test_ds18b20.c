/**
 * @file test_ds18b20.c
 * @brief Host tests for portable DS18B20 command, CRC, and conversion behavior.
 */

#include "ds18b20.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief Deterministic mock state for one injected 1-Wire bus. */
typedef struct
{
    sensor_status_t reset_status;
    sensor_status_t write_status;
    sensor_status_t read_status;
    uint8_t written[4];
    size_t written_length;
    uint8_t scratchpad[9];
} mock_onewire_t;

/**
 * @brief Return the configured mock presence result.
 * @param context Mock 1-Wire state.
 * @return Configured reset status.
 */
static sensor_status_t mock_onewire_reset(void *context)
{
    mock_onewire_t *mock = (mock_onewire_t *)context;

    return mock->reset_status;
}

/**
 * @brief Capture bytes written by the DS18B20 driver.
 * @param context Mock 1-Wire state.
 * @param data Bytes supplied by the driver.
 * @param length Number of supplied bytes.
 * @return Configured write status.
 */
static sensor_status_t mock_onewire_write(void *context,
                                          const uint8_t *data,
                                          size_t length)
{
    mock_onewire_t *mock = (mock_onewire_t *)context;

    assert(length <= sizeof(mock->written));
    memcpy(mock->written, data, length);
    mock->written_length = length;
    return mock->write_status;
}

/**
 * @brief Supply the configured scratchpad bytes.
 * @param context Mock 1-Wire state.
 * @param data Destination supplied by the driver.
 * @param length Number of requested bytes.
 * @return Configured read status.
 */
static sensor_status_t mock_onewire_read(void *context,
                                         uint8_t *data,
                                         size_t length)
{
    mock_onewire_t *mock = (mock_onewire_t *)context;

    if(mock->read_status == SENSOR_STATUS_OK)
    {
        assert(length == sizeof(mock->scratchpad));
        memcpy(data, mock->scratchpad, length);
    }
    return mock->read_status;
}

/**
 * @brief Calculate a fixture Dallas/Maxim CRC-8.
 * @param data Source bytes.
 * @param length Number of source bytes.
 * @return Fixture CRC value.
 */
static uint8_t fixture_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0U;

    while(length-- != 0U)
    {
        uint8_t value = *data++;
        uint8_t bit;

        for(bit = 0U; bit < 8U; bit++)
        {
            uint8_t mix = (uint8_t)((crc ^ value) & 0x01U);

            crc >>= 1;
            if(mix != 0U)
                crc ^= 0x8CU;
            value >>= 1;
        }
    }
    return crc;
}

/** @brief Initialize a bound sensor and a valid 25.0625 C scratchpad. */
static void prepare_sensor(mock_onewire_t *mock,
                           sensor_onewire_bus_t *bus,
                           ds18b20_t *sensor)
{
    memset(mock, 0, sizeof(*mock));
    memset(sensor, 0, sizeof(*sensor));
    mock->reset_status = SENSOR_STATUS_OK;
    mock->write_status = SENSOR_STATUS_OK;
    mock->read_status = SENSOR_STATUS_OK;
    mock->scratchpad[0] = 0x91U;
    mock->scratchpad[1] = 0x01U;
    mock->scratchpad[2] = 0x4BU;
    mock->scratchpad[3] = 0x46U;
    mock->scratchpad[4] = 0x7FU;
    mock->scratchpad[5] = 0xFFU;
    mock->scratchpad[6] = 0x0CU;
    mock->scratchpad[7] = 0x10U;
    mock->scratchpad[8] = fixture_crc8(mock->scratchpad, 8U);
    bus->context = mock;
    bus->reset = mock_onewire_reset;
    bus->write = mock_onewire_write;
    bus->read = mock_onewire_read;
    assert(ds18b20_init(sensor, bus) == SENSOR_STATUS_OK);
}

/** @brief Verify probe and conversion command sequencing. */
static void test_commands(void)
{
    mock_onewire_t mock;
    sensor_onewire_bus_t bus;
    ds18b20_t sensor;

    prepare_sensor(&mock, &bus, &sensor);
    assert(ds18b20_probe(&sensor) == SENSOR_STATUS_OK);
    assert(ds18b20_start_conversion(&sensor) == SENSOR_STATUS_OK);
    assert(mock.written_length == 2U);
    assert(mock.written[0] == 0xCCU);
    assert(mock.written[1] == 0x44U);
}

/** @brief Verify CRC-checked conversion of a known positive temperature. */
static void test_temperature_conversion(void)
{
    mock_onewire_t mock;
    sensor_onewire_bus_t bus;
    ds18b20_t sensor;
    float temperature_c = 0.0f;

    prepare_sensor(&mock, &bus, &sensor);
    assert(ds18b20_read_temperature(&sensor, &temperature_c) ==
           SENSOR_STATUS_OK);
    assert(mock.written_length == 2U);
    assert(mock.written[0] == 0xCCU);
    assert(mock.written[1] == 0xBEU);
    assert(fabsf(temperature_c - 25.0625f) < 0.001f);
}

/** @brief Verify presence, CRC, power-on, and bus errors remain distinct. */
static void test_typed_failures(void)
{
    mock_onewire_t mock;
    sensor_onewire_bus_t bus;
    ds18b20_t sensor;
    float temperature_c;

    prepare_sensor(&mock, &bus, &sensor);
    mock.reset_status = SENSOR_STATUS_NOT_PRESENT;
    assert(ds18b20_start_conversion(&sensor) == SENSOR_STATUS_NOT_PRESENT);

    mock.reset_status = SENSOR_STATUS_OK;
    mock.scratchpad[8] ^= 0x01U;
    assert(ds18b20_read_temperature(&sensor, &temperature_c) ==
           SENSOR_STATUS_CRC_ERROR);

    mock.scratchpad[0] = 0x50U;
    mock.scratchpad[1] = 0x05U;
    mock.scratchpad[8] = fixture_crc8(mock.scratchpad, 8U);
    assert(ds18b20_read_temperature(&sensor, &temperature_c) ==
           SENSOR_STATUS_NOT_READY);

    mock.read_status = SENSOR_STATUS_IO_ERROR;
    assert(ds18b20_read_temperature(&sensor, &temperature_c) ==
           SENSOR_STATUS_IO_ERROR);
    assert(ds18b20_read_temperature(&sensor, NULL) ==
           SENSOR_STATUS_INVALID_ARGUMENT);
}

/** @brief Run all portable DS18B20 host tests. */
int main(void)
{
    test_commands();
    test_temperature_conversion();
    test_typed_failures();
    return 0;
}
