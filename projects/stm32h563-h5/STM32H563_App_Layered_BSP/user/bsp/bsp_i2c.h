/**
 * @file bsp_i2c.h
 * @brief Logical board I2C interface configured in physical units.
 */

#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Logical board I2C roles. */
typedef enum
{
    BOARD_I2C_TOUCH = 0,
    BOARD_I2C_COUNT
} board_i2c_role_t;

/** @brief Requested I2C bitrate and measured/estimated edge times. */
typedef struct
{
    uint32_t bitrate_hz;
    uint32_t rise_time_ns;
    uint32_t fall_time_ns;
} bsp_i2c_config_t;

/** @brief Initialize one logical I2C bus using a TIMINGR solver. */
bsp_status_t bsp_i2c_init(board_i2c_role_t role, const bsp_i2c_config_t *config);
/** @brief Read the achieved physical I2C bitrate. */
bsp_status_t bsp_i2c_get_achieved_bitrate(board_i2c_role_t role,
                                          uint32_t *achieved_bitrate_hz);
/** @brief Probe one unshifted 7-bit address with bounded trials and timeout. */
bsp_status_t bsp_i2c_is_device_ready(board_i2c_role_t role,
                                     uint8_t address_7bit,
                                     uint32_t trials,
                                     uint32_t timeout_ms);
/** @brief Write bytes to one unshifted 7-bit address. */
bsp_status_t bsp_i2c_write(board_i2c_role_t role,
                           uint8_t address_7bit,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);
/** @brief Read bytes from one unshifted 7-bit address. */
bsp_status_t bsp_i2c_read(board_i2c_role_t role,
                          uint8_t address_7bit,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);
/** @brief Read bytes beginning at an 8-bit device register. */
bsp_status_t bsp_i2c_memory_read(board_i2c_role_t role,
                                 uint8_t address_7bit,
                                 uint8_t register_address,
                                 uint8_t *data,
                                 uint32_t length,
                                 uint32_t timeout_ms);
/** @brief Write bytes beginning at an 8-bit device register. */
bsp_status_t bsp_i2c_memory_write(board_i2c_role_t role,
                                  uint8_t address_7bit,
                                  uint8_t register_address,
                                  const uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms);

#endif /* BSP_I2C_H */
