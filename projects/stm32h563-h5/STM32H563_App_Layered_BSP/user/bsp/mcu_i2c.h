/**
 * @file mcu_i2c.h
 * @brief Private STM32H5 I2C timing solver and transfer interface.
 */

#ifndef MCU_I2C_H
#define MCU_I2C_H

#include <stdbool.h>

#include "bsp_i2c.h"
#include "stm32h5xx_hal.h"

/** @brief Static context owned by one logical I2C bus. */
typedef struct
{
    I2C_HandleTypeDef handle;
    uint32_t achieved_bitrate_hz;
    bool is_initialized;
} mcu_i2c_context_t;

/** @brief Initialize one STM32H5 I2C instance from physical timing values. */
bsp_status_t mcu_i2c_init(mcu_i2c_context_t *context,
                          I2C_TypeDef *instance,
                          uint32_t kernel_clock_hz,
                          const bsp_i2c_config_t *config);
/** @brief Probe one address with bounded trials. */
bsp_status_t mcu_i2c_is_device_ready(mcu_i2c_context_t *context,
                                     uint8_t address_7bit,
                                     uint32_t trials,
                                     uint32_t timeout_ms);
/** @brief Execute bounded master transmit. */
bsp_status_t mcu_i2c_write(mcu_i2c_context_t *context,
                           uint8_t address_7bit,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms);
/** @brief Execute bounded master receive. */
bsp_status_t mcu_i2c_read(mcu_i2c_context_t *context,
                          uint8_t address_7bit,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms);
/** @brief Execute bounded 8-bit register read. */
bsp_status_t mcu_i2c_memory_read(mcu_i2c_context_t *context,
                                 uint8_t address_7bit,
                                 uint8_t register_address,
                                 uint8_t *data,
                                 uint32_t length,
                                 uint32_t timeout_ms);
/** @brief Execute bounded 8-bit register write. */
bsp_status_t mcu_i2c_memory_write(mcu_i2c_context_t *context,
                                  uint8_t address_7bit,
                                  uint8_t register_address,
                                  const uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms);

#endif /* MCU_I2C_H */
