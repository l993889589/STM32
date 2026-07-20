/**
 * @file bsp_sensor.c
 * @brief Static CHPM bus adapters for the portable sensors library.
 */

#include "bsp_sensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_i2c.h"
#include "bsp_onewire.h"
#include "ds18b20.h"

static aht20_t aht20_sensor;
static ds18b20_t ds18b20_sensor;
static bool sensors_initialized;

/**
 * @brief Map one BSP status into the portable sensor status domain.
 * @param status BSP operation status.
 * @return Closest portable sensor status.
 */
static sensor_status_t bsp_sensor_map_status(bsp_status_t status)
{
    switch(status)
    {
        case BSP_STATUS_OK:
        case BSP_STATUS_ALREADY_INITIALIZED:
            return SENSOR_STATUS_OK;
        case BSP_STATUS_INVALID_ARGUMENT:
            return SENSOR_STATUS_INVALID_ARGUMENT;
        case BSP_STATUS_TIMEOUT:
            return SENSOR_STATUS_TIMEOUT;
        case BSP_STATUS_BUSY:
            return SENSOR_STATUS_BUSY;
        case BSP_STATUS_NOT_READY:
            return SENSOR_STATUS_NOT_READY;
        default:
            return SENSOR_STATUS_IO_ERROR;
    }
}

/**
 * @brief Adapt the board I2C write API to the portable sensor callback.
 * @param context Unused single-board bus context.
 * @param address_7bit Seven-bit slave address.
 * @param data Source bytes.
 * @param length Number of source bytes.
 * @return Portable transaction status.
 */
static sensor_status_t bsp_sensor_i2c_write(void *context,
                                            uint8_t address_7bit,
                                            const uint8_t *data,
                                            size_t length)
{
    (void)context;
    return bsp_sensor_map_status(bsp_i2c_write(address_7bit, data, length));
}

/**
 * @brief Adapt the board I2C read API to the portable sensor callback.
 * @param context Unused single-board bus context.
 * @param address_7bit Seven-bit slave address.
 * @param data Destination bytes.
 * @param length Number of requested bytes.
 * @return Portable transaction status.
 */
static sensor_status_t bsp_sensor_i2c_read(void *context,
                                           uint8_t address_7bit,
                                           uint8_t *data,
                                           size_t length)
{
    (void)context;
    return bsp_sensor_map_status(bsp_i2c_read(address_7bit, data, length));
}

/**
 * @brief Adapt the board 1-Wire presence operation.
 * @param context Unused single-board bus context.
 * @return OK, NOT_PRESENT, or another portable bus status.
 */
static sensor_status_t bsp_sensor_onewire_reset(void *context)
{
    bsp_status_t status;

    (void)context;
    status = bsp_onewire_reset();
    if(status == BSP_STATUS_IO_ERROR)
        return SENSOR_STATUS_NOT_PRESENT;
    return bsp_sensor_map_status(status);
}

/**
 * @brief Adapt the board 1-Wire write operation.
 * @param context Unused single-board bus context.
 * @param data Source bytes.
 * @param length Number of source bytes.
 * @return Portable transaction status.
 */
static sensor_status_t bsp_sensor_onewire_write(void *context,
                                                const uint8_t *data,
                                                size_t length)
{
    (void)context;
    return bsp_sensor_map_status(bsp_onewire_write(data, length));
}

/**
 * @brief Adapt the board 1-Wire read operation.
 * @param context Unused single-board bus context.
 * @param data Destination bytes.
 * @param length Number of requested bytes.
 * @return Portable transaction status.
 */
static sensor_status_t bsp_sensor_onewire_read(void *context,
                                               uint8_t *data,
                                               size_t length)
{
    (void)context;
    return bsp_sensor_map_status(bsp_onewire_read(data, length));
}

/**
 * @brief Bind the portable sensor contexts to the board buses.
 * @return BSP initialization status.
 */
bsp_status_t bsp_sensor_init(void)
{
    static const sensor_i2c_bus_t i2c_bus =
    {
        NULL,
        bsp_sensor_i2c_write,
        bsp_sensor_i2c_read
    };
    static const sensor_onewire_bus_t onewire_bus =
    {
        NULL,
        bsp_sensor_onewire_reset,
        bsp_sensor_onewire_write,
        bsp_sensor_onewire_read
    };
    bsp_status_t bsp_status;
    sensor_status_t sensor_status;

    if(sensors_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    bsp_status = bsp_onewire_init();
    if(bsp_status != BSP_STATUS_OK &&
       bsp_status != BSP_STATUS_ALREADY_INITIALIZED)
        return bsp_status;
    sensor_status = aht20_init(&aht20_sensor,
                               &i2c_bus,
                               AHT20_DEFAULT_ADDRESS_7BIT);
    if(sensor_status != SENSOR_STATUS_OK &&
       sensor_status != SENSOR_STATUS_ALREADY_INITIALIZED)
        return BSP_STATUS_IO_ERROR;
    sensor_status = ds18b20_init(&ds18b20_sensor, &onewire_bus);
    if(sensor_status != SENSOR_STATUS_OK &&
       sensor_status != SENSOR_STATUS_ALREADY_INITIALIZED)
        return BSP_STATUS_IO_ERROR;

    sensors_initialized = true;
    return BSP_STATUS_OK;
}

/**
 * @brief Send the AHT20 soft-reset command.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_reset(void)
{
    return sensors_initialized ?
           aht20_reset(&aht20_sensor) :
           SENSOR_STATUS_NOT_READY;
}

/**
 * @brief Send the AHT20 calibration initialization command.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_configure(void)
{
    return sensors_initialized ?
           aht20_configure(&aht20_sensor) :
           SENSOR_STATUS_NOT_READY;
}

/**
 * @brief Start one AHT20 environmental conversion.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_start_measurement(void)
{
    return sensors_initialized ?
           aht20_start_measurement(&aht20_sensor) :
           SENSOR_STATUS_NOT_READY;
}

/**
 * @brief Read one completed AHT20 environmental sample.
 * @param measurement Destination for converted values.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_read_measurement(
    aht20_measurement_t *measurement)
{
    return sensors_initialized ?
           aht20_read_measurement(&aht20_sensor, measurement) :
           SENSOR_STATUS_NOT_READY;
}

/**
 * @brief Probe the PB0 single-drop bus for the DS18B20.
 * @return Typed presence or bus status.
 */
sensor_status_t bsp_sensor_ds18b20_probe(void)
{
    return sensors_initialized ?
           ds18b20_probe(&ds18b20_sensor) :
           SENSOR_STATUS_NOT_READY;
}

/**
 * @brief Start one DS18B20 temperature conversion.
 * @return Typed presence or bus status.
 */
sensor_status_t bsp_sensor_ds18b20_start_conversion(void)
{
    return sensors_initialized ?
           ds18b20_start_conversion(&ds18b20_sensor) :
           SENSOR_STATUS_NOT_READY;
}

/**
 * @brief Read one completed and CRC-checked DS18B20 temperature.
 * @param temperature_c Destination for the Celsius value.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_ds18b20_read_temperature(float *temperature_c)
{
    return sensors_initialized ?
           ds18b20_read_temperature(&ds18b20_sensor, temperature_c) :
           SENSOR_STATUS_NOT_READY;
}
