/**
 * @file mcu_i2c.h
 * @brief Private STM32H5 I2C timing solver and transfer interface.
 */

#ifndef MCU_I2C_H
#define MCU_I2C_H

#include <stdbool.h>

#include "bsp_i2c.h"
#include "stm32h5xx_hal.h"

typedef struct
{
    I2C_HandleTypeDef handle;
    uint32_t achieved_bitrate_hz;
    bool is_initialized;
} mcu_i2c_context_t;

/**
 * @brief Initialize one STM32H5 I2C instance using a solved TIMINGR value.
 * @param context Static bus context owned by one board role.
 * @param instance STM32 I2C peripheral instance.
 * @param kernel_clock_hz Actual I2C kernel clock in hertz.
 * @param config Requested bus timing in physical units.
 * @return BSP status.
 */
bsp_status_t mcu_i2c_init(mcu_i2c_context_t *context,
                                  I2C_TypeDef *instance,
                                  uint32_t kernel_clock_hz,
                                  const bsp_i2c_config_t *config);

/**
 * @brief Execute a blocking STM32H5 I2C master transmit.
 * @param context Initialized bus context.
 * @param address_7bit Unshifted 7-bit address.
 * @param data Source bytes.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time.
 * @return BSP status.
 */
bsp_status_t mcu_i2c_write(mcu_i2c_context_t *context,
                                   uint8_t address_7bit,
                                   const uint8_t *data,
                                   uint32_t length,
                                   uint32_t timeout_ms);

/**
 * @brief Execute a blocking STM32H5 I2C master receive.
 * @param context Initialized bus context.
 * @param address_7bit Unshifted 7-bit address.
 * @param data Destination bytes.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time.
 * @return BSP status.
 */
bsp_status_t mcu_i2c_read(mcu_i2c_context_t *context,
                                  uint8_t address_7bit,
                                  uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms);

/**
 * @brief Execute a blocking STM32H5 8-bit memory/register read.
 * @param context Initialized bus context.
 * @param address_7bit Unshifted 7-bit address.
 * @param register_address Eight-bit register address.
 * @param data Destination bytes.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time.
 * @return BSP status.
 */
bsp_status_t mcu_i2c_memory_read(mcu_i2c_context_t *context,
                                         uint8_t address_7bit,
                                         uint8_t register_address,
                                         uint8_t *data,
                                         uint32_t length,
                                         uint32_t timeout_ms);

/**
 * @brief Execute a blocking STM32H5 8-bit memory/register write.
 * @param context Initialized bus context.
 * @param address_7bit Unshifted 7-bit address.
 * @param register_address Eight-bit register address.
 * @param data Source bytes.
 * @param length Byte count.
 * @param timeout_ms Maximum blocking time.
 * @return BSP status.
 */
bsp_status_t mcu_i2c_memory_write(mcu_i2c_context_t *context,
                                          uint8_t address_7bit,
                                          uint8_t register_address,
                                          const uint8_t *data,
                                          uint32_t length,
                                          uint32_t timeout_ms);

#endif
