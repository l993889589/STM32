/**
 * @file bsp_i2c.h
 * @brief Logical board I2C interface configured in physical units.
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    BOARD_I2C_TOUCH = 0,
    BOARD_I2C_COUNT
} board_i2c_role_t;

typedef struct
{
    uint32_t bitrate_hz;
    uint32_t rise_time_ns;
    uint32_t fall_time_ns;
} bsp_i2c_config_t;

/**
 * @brief Initialize a logical I2C bus and solve TIMINGR from the requested bitrate.
 * @param role Logical I2C role.
 * @param config Bitrate and board rise/fall estimates in physical units.
 * @return BSP status.
 */
bsp_status_t bsp_i2c_init(board_i2c_role_t role,
                          const bsp_i2c_config_t *config);
/**
 * Read the physical I2C bit rate selected by the TIMINGR solver.
 * @param role Logical I2C role.
 * @param achieved_bitrate_hz Receives the actual bit rate in hertz.
 * @return BSP status.
 */
bsp_status_t bsp_i2c_get_achieved_bitrate(
    board_i2c_role_t role,
    uint32_t *achieved_bitrate_hz);

/**
 * @brief Write bytes to one 7-bit I2C address with a bounded timeout.
 * @param role Logical I2C role.
 * @param address_7bit Unshifted 7-bit device address.
 * @param data Source bytes valid until the call returns.
 * @param length Number of bytes to write.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_i2c_write(board_i2c_role_t role,
                           uint8_t address_7bit,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);

/**
 * @brief Read bytes from one 7-bit I2C address with a bounded timeout.
 * @param role Logical I2C role.
 * @param address_7bit Unshifted 7-bit device address.
 * @param data Destination buffer.
 * @param length Number of bytes to read.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_i2c_read(board_i2c_role_t role,
                          uint8_t address_7bit,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);

/**
 * @brief Read bytes beginning at an 8-bit device register.
 * @param role Logical I2C role.
 * @param address_7bit Unshifted 7-bit device address.
 * @param register_address Eight-bit register address.
 * @param data Destination buffer.
 * @param length Number of bytes to read.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_i2c_memory_read(board_i2c_role_t role,
                                 uint8_t address_7bit,
                                 uint8_t register_address,
                                 uint8_t *data,
                                 uint32_t length,
                                 uint32_t timeout_ms);

/**
 * @brief Write bytes beginning at an 8-bit device register.
 * @param role Logical I2C role.
 * @param address_7bit Unshifted 7-bit device address.
 * @param register_address Eight-bit register address.
 * @param data Source buffer.
 * @param length Number of bytes to write.
 * @param timeout_ms Maximum blocking time in milliseconds.
 * @return BSP status.
 */
bsp_status_t bsp_i2c_memory_write(board_i2c_role_t role,
                                  uint8_t address_7bit,
                                  uint8_t register_address,
                                  const uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms);

#endif
