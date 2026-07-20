/**
 * @file aht20.h
 * @brief Portable AHT20 command and measurement conversion API.
 */

#ifndef AHT20_H
#define AHT20_H

#include <stdbool.h>
#include <stdint.h>

#include "sensor_bus.h"

#define AHT20_DEFAULT_ADDRESS_7BIT (0x38U)

/** @brief Bound AHT20 instance with no dynamic allocation. */
typedef struct
{
    sensor_i2c_bus_t bus;
    uint8_t address_7bit;
    bool is_initialized;
} aht20_t;

/** @brief One decoded AHT20 environmental sample. */
typedef struct
{
    float temperature_c;
    float humidity_percent;
} aht20_measurement_t;

/**
 * @brief Bind an AHT20 context to an injected I2C bus.
 * @param sensor Static sensor context owned by the caller.
 * @param bus I2C callbacks that remain valid for the context lifetime.
 * @param address_7bit Seven-bit device address, normally 0x38.
 * @return OK, ALREADY_INITIALIZED, or INVALID_ARGUMENT.
 */
sensor_status_t aht20_init(aht20_t *sensor,
                           const sensor_i2c_bus_t *bus,
                           uint8_t address_7bit);

/**
 * @brief Send the AHT20 soft-reset command.
 * @param sensor Initialized sensor context.
 * @return Bus status or NOT_READY for an unbound context.
 */
sensor_status_t aht20_reset(aht20_t *sensor);

/**
 * @brief Send the AHT20 calibration initialization command.
 * @param sensor Initialized sensor context.
 * @return Bus status or NOT_READY for an unbound context.
 */
sensor_status_t aht20_configure(aht20_t *sensor);

/**
 * @brief Trigger one humidity and temperature conversion.
 * @param sensor Initialized sensor context.
 * @return Bus status or NOT_READY for an unbound context.
 */
sensor_status_t aht20_start_measurement(aht20_t *sensor);

/**
 * @brief Read the current AHT20 status byte.
 * @param sensor Initialized sensor context.
 * @param status Destination for the status byte.
 * @return Bus status, INVALID_ARGUMENT, or NOT_READY.
 */
sensor_status_t aht20_read_status(aht20_t *sensor, uint8_t *status);

/**
 * @brief Read and convert one completed AHT20 measurement.
 * @param sensor Initialized sensor context.
 * @param measurement Destination for Celsius and relative-humidity values.
 * @return OK or a typed busy, calibration, bus, or range error.
 */
sensor_status_t aht20_read_measurement(aht20_t *sensor,
                                       aht20_measurement_t *measurement);

#endif /* AHT20_H */
