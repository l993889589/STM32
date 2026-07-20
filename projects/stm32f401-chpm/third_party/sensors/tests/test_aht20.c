/**
 * @file test_aht20.c
 * @brief Host tests for portable AHT20 command and conversion behavior.
 */

#include "aht20.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief Deterministic mock state for one injected I2C bus. */
typedef struct
{
    sensor_status_t write_status;
    sensor_status_t read_status;
    uint8_t write_address;
    uint8_t write_data[3];
    size_t write_length;
    uint8_t read_data[6];
} mock_i2c_t;

/**
 * @brief Capture one AHT20 write transaction.
 * @param context Mock bus state.
 * @param address_7bit Address supplied by the driver.
 * @param data Bytes supplied by the driver.
 * @param length Number of bytes supplied.
 * @return Configured mock write status.
 */
static sensor_status_t mock_i2c_write(void *context,
                                      uint8_t address_7bit,
                                      const uint8_t *data,
                                      size_t length)
{
    mock_i2c_t *mock = (mock_i2c_t *)context;

    assert(length <= sizeof(mock->write_data));
    mock->write_address = address_7bit;
    mock->write_length = length;
    memcpy(mock->write_data, data, length);
    return mock->write_status;
}

/**
 * @brief Supply one deterministic AHT20 read transaction.
 * @param context Mock bus state.
 * @param address_7bit Address supplied by the driver.
 * @param data Destination supplied by the driver.
 * @param length Number of requested bytes.
 * @return Configured mock read status.
 */
static sensor_status_t mock_i2c_read(void *context,
                                     uint8_t address_7bit,
                                     uint8_t *data,
                                     size_t length)
{
    mock_i2c_t *mock = (mock_i2c_t *)context;

    assert(address_7bit == AHT20_DEFAULT_ADDRESS_7BIT);
    if(mock->read_status == SENSOR_STATUS_OK)
    {
        assert(length <= sizeof(mock->read_data));
        memcpy(data, mock->read_data, length);
    }
    return mock->read_status;
}

/** @brief Verify binding validation and the three AHT20 command frames. */
static void test_commands(void)
{
    mock_i2c_t mock = {SENSOR_STATUS_OK, SENSOR_STATUS_OK, 0U, {0U}, 0U, {0U}};
    sensor_i2c_bus_t bus = {&mock, mock_i2c_write, mock_i2c_read};
    aht20_t sensor = {0};

    assert(aht20_init(NULL, &bus, AHT20_DEFAULT_ADDRESS_7BIT) ==
           SENSOR_STATUS_INVALID_ARGUMENT);
    assert(aht20_init(&sensor, &bus, AHT20_DEFAULT_ADDRESS_7BIT) ==
           SENSOR_STATUS_OK);
    assert(aht20_init(&sensor, &bus, AHT20_DEFAULT_ADDRESS_7BIT) ==
           SENSOR_STATUS_ALREADY_INITIALIZED);

    assert(aht20_reset(&sensor) == SENSOR_STATUS_OK);
    assert(mock.write_address == AHT20_DEFAULT_ADDRESS_7BIT);
    assert(mock.write_length == 1U);
    assert(mock.write_data[0] == 0xBAU);

    assert(aht20_configure(&sensor) == SENSOR_STATUS_OK);
    assert(mock.write_length == 3U);
    assert(mock.write_data[0] == 0xE1U);
    assert(mock.write_data[1] == 0x08U);
    assert(mock.write_data[2] == 0x00U);

    assert(aht20_start_measurement(&sensor) == SENSOR_STATUS_OK);
    assert(mock.write_length == 3U);
    assert(mock.write_data[0] == 0xACU);
    assert(mock.write_data[1] == 0x33U);
    assert(mock.write_data[2] == 0x00U);
}

/** @brief Verify conversion of a known 25 C and 50 percent sample. */
static void test_measurement_conversion(void)
{
    mock_i2c_t mock = {SENSOR_STATUS_OK, SENSOR_STATUS_OK, 0U, {0U}, 0U,
                       {0x08U, 0x80U, 0x00U, 0x06U, 0x00U, 0x00U}};
    sensor_i2c_bus_t bus = {&mock, mock_i2c_write, mock_i2c_read};
    aht20_t sensor = {0};
    aht20_measurement_t measurement = {0.0f, 0.0f};

    assert(aht20_init(&sensor, &bus, AHT20_DEFAULT_ADDRESS_7BIT) ==
           SENSOR_STATUS_OK);
    assert(aht20_read_measurement(&sensor, &measurement) == SENSOR_STATUS_OK);
    assert(fabsf(measurement.temperature_c - 25.0f) < 0.001f);
    assert(fabsf(measurement.humidity_percent - 50.0f) < 0.001f);
}

/** @brief Verify busy, calibration, and bus failures remain distinguishable. */
static void test_typed_failures(void)
{
    mock_i2c_t mock = {SENSOR_STATUS_OK, SENSOR_STATUS_OK, 0U, {0U}, 0U, {0U}};
    sensor_i2c_bus_t bus = {&mock, mock_i2c_write, mock_i2c_read};
    aht20_t sensor = {0};
    aht20_measurement_t measurement;

    assert(aht20_init(&sensor, &bus, AHT20_DEFAULT_ADDRESS_7BIT) ==
           SENSOR_STATUS_OK);
    mock.read_data[0] = 0x80U;
    assert(aht20_read_measurement(&sensor, &measurement) == SENSOR_STATUS_BUSY);
    mock.read_data[0] = 0x00U;
    assert(aht20_read_measurement(&sensor, &measurement) ==
           SENSOR_STATUS_NOT_READY);
    mock.read_status = SENSOR_STATUS_TIMEOUT;
    assert(aht20_read_measurement(&sensor, &measurement) ==
           SENSOR_STATUS_TIMEOUT);
    assert(aht20_read_measurement(&sensor, NULL) ==
           SENSOR_STATUS_INVALID_ARGUMENT);
}

/** @brief Run all portable AHT20 host tests. */
int main(void)
{
    test_commands();
    test_measurement_conversion();
    test_typed_failures();
    return 0;
}
