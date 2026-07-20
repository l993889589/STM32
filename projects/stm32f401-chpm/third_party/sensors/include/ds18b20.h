/**
 * @file ds18b20.h
 * @brief Portable single-drop DS18B20 protocol and conversion API.
 */

#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>

#include "sensor_bus.h"

/** @brief Bound DS18B20 instance with no dynamic allocation. */
typedef struct
{
    sensor_onewire_bus_t bus;
    bool is_initialized;
} ds18b20_t;

/**
 * @brief Bind a DS18B20 context to an injected 1-Wire bus.
 * @param sensor Static sensor context owned by the caller.
 * @param bus 1-Wire callbacks that remain valid for the context lifetime.
 * @return OK, ALREADY_INITIALIZED, or INVALID_ARGUMENT.
 */
sensor_status_t ds18b20_init(ds18b20_t *sensor,
                             const sensor_onewire_bus_t *bus);

/**
 * @brief Probe the single-drop bus for a responding DS18B20.
 * @param sensor Initialized sensor context.
 * @return OK, NOT_PRESENT, or another typed bus status.
 */
sensor_status_t ds18b20_probe(ds18b20_t *sensor);

/**
 * @brief Start a temperature conversion using Skip ROM.
 * @param sensor Initialized single-drop sensor context.
 * @return Typed bus status.
 */
sensor_status_t ds18b20_start_conversion(ds18b20_t *sensor);

/**
 * @brief Read, CRC-check, and convert the nine-byte scratchpad.
 * @param sensor Initialized single-drop sensor context.
 * @param temperature_c Destination for the Celsius value.
 * @return OK or a typed presence, bus, CRC, readiness, or range error.
 */
sensor_status_t ds18b20_read_temperature(ds18b20_t *sensor,
                                         float *temperature_c);

#endif /* DS18B20_H */
