/**
 * @file bsp_i2c.h
 * @brief PB6/PB7 software-I2C transactions for board sensors.
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "bsp_status.h"

/** @brief Initialize the open-drain bus and recover it to the idle state. */
bsp_status_t bsp_i2c_init(void);

/** @brief Write a command buffer to one 7-bit slave address. */
bsp_status_t bsp_i2c_write(uint8_t address_7bit,
                           const uint8_t *data,
                           size_t length);

/** @brief Read bytes from one 7-bit slave address. */
bsp_status_t bsp_i2c_read(uint8_t address_7bit,
                          uint8_t *data,
                          size_t length);

/** @brief Probe a 7-bit slave address for an acknowledge. */
bsp_status_t bsp_i2c_probe(uint8_t address_7bit);

#endif /* BSP_I2C_H */
