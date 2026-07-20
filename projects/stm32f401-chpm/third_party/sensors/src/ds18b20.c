/**
 * @file ds18b20.c
 * @brief Pure C DS18B20 protocol implementation over injected 1-Wire I/O.
 */

#include "ds18b20.h"

#include <stddef.h>
#include <stdint.h>

#define DS18B20_SKIP_ROM_CMD          (0xCCU)
#define DS18B20_CONVERT_T_CMD         (0x44U)
#define DS18B20_READ_SCRATCHPAD_CMD   (0xBEU)
#define DS18B20_POWER_ON_RAW_VALUE    (0x0550)
#define DS18B20_DEGREES_PER_RAW_COUNT (0.0625f)

/**
 * @brief Check that a DS18B20 context has a complete bus binding.
 * @param sensor Sensor context to inspect.
 * @return true only for a usable initialized context.
 */
static bool ds18b20_is_ready(const ds18b20_t *sensor)
{
    return sensor != NULL &&
           sensor->is_initialized &&
           sensor->bus.reset != NULL &&
           sensor->bus.write != NULL &&
           sensor->bus.read != NULL;
}

/**
 * @brief Calculate Dallas/Maxim CRC-8 over a byte sequence.
 * @param data Source bytes.
 * @param length Number of bytes included in the CRC.
 * @return Dallas/Maxim CRC-8 value.
 */
static uint8_t ds18b20_crc8(const uint8_t *data, size_t length)
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

/**
 * @brief Bind a DS18B20 context to an injected 1-Wire bus.
 * @param sensor Static sensor context owned by the caller.
 * @param bus 1-Wire callbacks that remain valid for the context lifetime.
 * @return Context initialization status.
 */
sensor_status_t ds18b20_init(ds18b20_t *sensor,
                             const sensor_onewire_bus_t *bus)
{
    if(sensor == NULL || bus == NULL ||
       bus->reset == NULL || bus->write == NULL || bus->read == NULL)
        return SENSOR_STATUS_INVALID_ARGUMENT;
    if(sensor->is_initialized)
        return SENSOR_STATUS_ALREADY_INITIALIZED;

    sensor->bus = *bus;
    sensor->is_initialized = true;
    return SENSOR_STATUS_OK;
}

/**
 * @brief Probe the single-drop bus for a responding DS18B20.
 * @param sensor Initialized sensor context.
 * @return Typed bus status.
 */
sensor_status_t ds18b20_probe(ds18b20_t *sensor)
{
    if(!ds18b20_is_ready(sensor))
        return SENSOR_STATUS_NOT_READY;
    return sensor->bus.reset(sensor->bus.context);
}

/**
 * @brief Start a temperature conversion using Skip ROM.
 * @param sensor Initialized single-drop sensor context.
 * @return Typed bus status.
 */
sensor_status_t ds18b20_start_conversion(ds18b20_t *sensor)
{
    static const uint8_t command[] =
    {
        DS18B20_SKIP_ROM_CMD,
        DS18B20_CONVERT_T_CMD
    };
    sensor_status_t status;

    if(!ds18b20_is_ready(sensor))
        return SENSOR_STATUS_NOT_READY;
    status = sensor->bus.reset(sensor->bus.context);
    if(status != SENSOR_STATUS_OK)
        return status;
    return sensor->bus.write(sensor->bus.context,
                             command,
                             sizeof(command));
}

/**
 * @brief Read, CRC-check, and convert the nine-byte scratchpad.
 * @param sensor Initialized single-drop sensor context.
 * @param temperature_c Destination for the Celsius value.
 * @return Typed validation, bus, CRC, readiness, or range status.
 */
sensor_status_t ds18b20_read_temperature(ds18b20_t *sensor,
                                         float *temperature_c)
{
    static const uint8_t command[] =
    {
        DS18B20_SKIP_ROM_CMD,
        DS18B20_READ_SCRATCHPAD_CMD
    };
    uint8_t scratchpad[9];
    int16_t raw;
    float measured;
    sensor_status_t status;

    if(temperature_c == NULL)
        return SENSOR_STATUS_INVALID_ARGUMENT;
    if(!ds18b20_is_ready(sensor))
        return SENSOR_STATUS_NOT_READY;

    status = sensor->bus.reset(sensor->bus.context);
    if(status != SENSOR_STATUS_OK)
        return status;
    status = sensor->bus.write(sensor->bus.context,
                               command,
                               sizeof(command));
    if(status != SENSOR_STATUS_OK)
        return status;
    status = sensor->bus.read(sensor->bus.context,
                              scratchpad,
                              sizeof(scratchpad));
    if(status != SENSOR_STATUS_OK)
        return status;
    if(ds18b20_crc8(scratchpad, 8U) != scratchpad[8])
        return SENSOR_STATUS_CRC_ERROR;

    raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    if(raw == DS18B20_POWER_ON_RAW_VALUE)
        return SENSOR_STATUS_NOT_READY;
    measured = (float)raw * DS18B20_DEGREES_PER_RAW_COUNT;
    if(measured < -55.0f || measured > 125.0f)
        return SENSOR_STATUS_RANGE_ERROR;

    *temperature_c = measured;
    return SENSOR_STATUS_OK;
}
