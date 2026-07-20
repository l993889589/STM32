/**
 * @file aht20.c
 * @brief Pure C AHT20 protocol implementation over injected I2C callbacks.
 */

#include "aht20.h"

#include <stddef.h>

#define AHT20_INIT_CMD          (0xE1U)
#define AHT20_MEASURE_CMD       (0xACU)
#define AHT20_RESET_CMD         (0xBAU)
#define AHT20_STATUS_BUSY       (0x80U)
#define AHT20_STATUS_CALIBRATED (0x08U)

/**
 * @brief Check that an AHT20 context has a complete bus binding.
 * @param sensor Sensor context to inspect.
 * @return true only for a usable initialized context.
 */
static bool aht20_is_ready(const aht20_t *sensor)
{
    return sensor != NULL &&
           sensor->is_initialized &&
           sensor->bus.write != NULL &&
           sensor->bus.read != NULL;
}

/**
 * @brief Send one command and up to two parameter bytes.
 * @param sensor Initialized sensor context.
 * @param command AHT20 command byte.
 * @param parameters Optional parameter bytes.
 * @param parameter_count Number of parameter bytes.
 * @return Typed validation or bus status.
 */
static sensor_status_t aht20_write_command(aht20_t *sensor,
                                           uint8_t command,
                                           const uint8_t *parameters,
                                           uint8_t parameter_count)
{
    uint8_t frame[3];
    uint8_t index;

    if(!aht20_is_ready(sensor))
        return SENSOR_STATUS_NOT_READY;
    if(parameter_count > 2U ||
       (parameter_count != 0U && parameters == NULL))
        return SENSOR_STATUS_INVALID_ARGUMENT;

    frame[0] = command;
    for(index = 0U; index < parameter_count; index++)
        frame[index + 1U] = parameters[index];
    return sensor->bus.write(sensor->bus.context,
                             sensor->address_7bit,
                             frame,
                             (size_t)parameter_count + 1U);
}

/**
 * @brief Bind an AHT20 context to an injected I2C bus.
 * @param sensor Static sensor context owned by the caller.
 * @param bus I2C callbacks that remain valid for the context lifetime.
 * @param address_7bit Seven-bit device address.
 * @return Context initialization status.
 */
sensor_status_t aht20_init(aht20_t *sensor,
                           const sensor_i2c_bus_t *bus,
                           uint8_t address_7bit)
{
    if(sensor == NULL || bus == NULL ||
       bus->write == NULL || bus->read == NULL ||
       address_7bit > 0x7FU)
        return SENSOR_STATUS_INVALID_ARGUMENT;
    if(sensor->is_initialized)
        return SENSOR_STATUS_ALREADY_INITIALIZED;

    sensor->bus = *bus;
    sensor->address_7bit = address_7bit;
    sensor->is_initialized = true;
    return SENSOR_STATUS_OK;
}

/**
 * @brief Send the AHT20 soft-reset command.
 * @param sensor Initialized sensor context.
 * @return Typed validation or bus status.
 */
sensor_status_t aht20_reset(aht20_t *sensor)
{
    return aht20_write_command(sensor, AHT20_RESET_CMD, NULL, 0U);
}

/**
 * @brief Send the AHT20 calibration initialization command.
 * @param sensor Initialized sensor context.
 * @return Typed validation or bus status.
 */
sensor_status_t aht20_configure(aht20_t *sensor)
{
    static const uint8_t parameters[] = {0x08U, 0x00U};

    return aht20_write_command(sensor,
                               AHT20_INIT_CMD,
                               parameters,
                               (uint8_t)sizeof(parameters));
}

/**
 * @brief Trigger one humidity and temperature conversion.
 * @param sensor Initialized sensor context.
 * @return Typed validation or bus status.
 */
sensor_status_t aht20_start_measurement(aht20_t *sensor)
{
    static const uint8_t parameters[] = {0x33U, 0x00U};

    return aht20_write_command(sensor,
                               AHT20_MEASURE_CMD,
                               parameters,
                               (uint8_t)sizeof(parameters));
}

/**
 * @brief Read the current AHT20 status byte.
 * @param sensor Initialized sensor context.
 * @param status Destination for the status byte.
 * @return Typed validation or bus status.
 */
sensor_status_t aht20_read_status(aht20_t *sensor, uint8_t *status)
{
    if(status == NULL)
        return SENSOR_STATUS_INVALID_ARGUMENT;
    if(!aht20_is_ready(sensor))
        return SENSOR_STATUS_NOT_READY;
    return sensor->bus.read(sensor->bus.context,
                            sensor->address_7bit,
                            status,
                            1U);
}

/**
 * @brief Read and convert one completed AHT20 measurement.
 * @param sensor Initialized sensor context.
 * @param measurement Destination for converted values.
 * @return Typed validation, readiness, bus, or data status.
 */
sensor_status_t aht20_read_measurement(aht20_t *sensor,
                                       aht20_measurement_t *measurement)
{
    uint8_t data[6];
    uint32_t raw_humidity;
    uint32_t raw_temperature;
    float measured_temperature;
    float measured_humidity;
    sensor_status_t status;

    if(measurement == NULL)
        return SENSOR_STATUS_INVALID_ARGUMENT;
    if(!aht20_is_ready(sensor))
        return SENSOR_STATUS_NOT_READY;

    status = sensor->bus.read(sensor->bus.context,
                              sensor->address_7bit,
                              data,
                              sizeof(data));
    if(status != SENSOR_STATUS_OK)
        return status;
    if((data[0] & AHT20_STATUS_BUSY) != 0U)
        return SENSOR_STATUS_BUSY;
    if((data[0] & AHT20_STATUS_CALIBRATED) == 0U)
        return SENSOR_STATUS_NOT_READY;

    raw_humidity = (((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) |
                    data[3]) >> 4;
    raw_temperature = ((uint32_t)(data[3] & 0x0FU) << 16) |
                      ((uint32_t)data[4] << 8) |
                      data[5];
    measured_humidity = (float)raw_humidity * 100.0f / 1048576.0f;
    measured_temperature = (float)raw_temperature * 200.0f /
                           1048576.0f - 50.0f;
    if(measured_temperature < -50.0f || measured_temperature > 150.0f ||
       measured_humidity < 0.0f || measured_humidity > 100.0f)
        return SENSOR_STATUS_RANGE_ERROR;

    measurement->temperature_c = measured_temperature;
    measurement->humidity_percent = measured_humidity;
    return SENSOR_STATUS_OK;
}
