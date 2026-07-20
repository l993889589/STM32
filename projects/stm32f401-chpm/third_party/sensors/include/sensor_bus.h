/**
 * @file sensor_bus.h
 * @brief Injected bus contracts used by the portable sensor drivers.
 */

#ifndef SENSOR_BUS_H
#define SENSOR_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "sensor_status.h"

/**
 * @brief Write one complete I2C transaction.
 * @param context Caller-provided bus instance.
 * @param address_7bit Seven-bit slave address.
 * @param data Bytes valid until the callback returns.
 * @param length Number of bytes to write.
 * @return Portable transaction status.
 */
typedef sensor_status_t (*sensor_i2c_write_fn)(void *context,
                                               uint8_t address_7bit,
                                               const uint8_t *data,
                                               size_t length);

/**
 * @brief Read one complete I2C transaction.
 * @param context Caller-provided bus instance.
 * @param address_7bit Seven-bit slave address.
 * @param data Destination valid until the callback returns.
 * @param length Number of bytes to read.
 * @return Portable transaction status.
 */
typedef sensor_status_t (*sensor_i2c_read_fn)(void *context,
                                              uint8_t address_7bit,
                                              uint8_t *data,
                                              size_t length);

/** @brief I2C dependency bundle injected into an I2C sensor context. */
typedef struct
{
    void *context;
    sensor_i2c_write_fn write;
    sensor_i2c_read_fn read;
} sensor_i2c_bus_t;

/**
 * @brief Reset a 1-Wire bus and validate a presence pulse.
 * @param context Caller-provided bus instance.
 * @return OK when a device responds, otherwise a typed bus status.
 */
typedef sensor_status_t (*sensor_onewire_reset_fn)(void *context);

/**
 * @brief Write bytes over an already-reset 1-Wire transaction.
 * @param context Caller-provided bus instance.
 * @param data Bytes valid until the callback returns.
 * @param length Number of bytes to write.
 * @return Portable transaction status.
 */
typedef sensor_status_t (*sensor_onewire_write_fn)(void *context,
                                                   const uint8_t *data,
                                                   size_t length);

/**
 * @brief Read bytes over an already-reset 1-Wire transaction.
 * @param context Caller-provided bus instance.
 * @param data Destination valid until the callback returns.
 * @param length Number of bytes to read.
 * @return Portable transaction status.
 */
typedef sensor_status_t (*sensor_onewire_read_fn)(void *context,
                                                  uint8_t *data,
                                                  size_t length);

/** @brief 1-Wire dependency bundle injected into a 1-Wire sensor context. */
typedef struct
{
    void *context;
    sensor_onewire_reset_fn reset;
    sensor_onewire_write_fn write;
    sensor_onewire_read_fn read;
} sensor_onewire_bus_t;

#endif /* SENSOR_BUS_H */
