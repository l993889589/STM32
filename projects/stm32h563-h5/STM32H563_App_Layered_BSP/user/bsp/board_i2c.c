/**
 * @file board_i2c.c
 * @brief I2C1 touch-bus board binding.
 */

#include "bsp_i2c.h"

#include <stddef.h>

#include "board_resources.h"
#include "mcu_i2c.h"

static mcu_i2c_context_t g_touch_i2c_context;

/** @brief Implement bsp_i2c_init() for the PB8/PB9 I2C1 binding. */
bsp_status_t bsp_i2c_init(board_i2c_role_t role, const bsp_i2c_config_t *config)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    uint32_t kernel_clock_hz;

    if(role != BOARD_I2C_TOUCH)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    peripheral_clock.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    kernel_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C1);
    if(kernel_clock_hz == 0U)
    {
        return BSP_STATUS_IO_ERROR;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    gpio.Pin = BOARD_I2C_TOUCH_SCL_PIN | BOARD_I2C_TOUCH_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = BOARD_I2C_TOUCH_SCL_AF;
    HAL_GPIO_Init(BOARD_I2C_TOUCH_SCL_PORT, &gpio);

    return mcu_i2c_init(&g_touch_i2c_context,
                        BOARD_I2C_TOUCH_INSTANCE,
                        kernel_clock_hz,
                        config);
}

/** @brief Implement achieved-bitrate query for the touch bus. */
bsp_status_t bsp_i2c_get_achieved_bitrate(board_i2c_role_t role,
                                          uint32_t *achieved_bitrate_hz)
{
    if((role != BOARD_I2C_TOUCH) || (achieved_bitrate_hz == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!g_touch_i2c_context.is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *achieved_bitrate_hz = g_touch_i2c_context.achieved_bitrate_hz;
    return BSP_STATUS_OK;
}

/** @brief Implement address readiness probe for the touch bus. */
bsp_status_t bsp_i2c_is_device_ready(board_i2c_role_t role,
                                     uint8_t address_7bit,
                                     uint32_t trials,
                                     uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           mcu_i2c_is_device_ready(&g_touch_i2c_context,
                                   address_7bit,
                                   trials,
                                   timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement logical touch-bus transmit. */
bsp_status_t bsp_i2c_write(board_i2c_role_t role,
                           uint8_t address_7bit,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           mcu_i2c_write(&g_touch_i2c_context,
                         address_7bit,
                         data,
                         length,
                         timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement logical touch-bus receive. */
bsp_status_t bsp_i2c_read(board_i2c_role_t role,
                          uint8_t address_7bit,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           mcu_i2c_read(&g_touch_i2c_context,
                        address_7bit,
                        data,
                        length,
                        timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement logical touch-bus register read. */
bsp_status_t bsp_i2c_memory_read(board_i2c_role_t role,
                                 uint8_t address_7bit,
                                 uint8_t register_address,
                                 uint8_t *data,
                                 uint32_t length,
                                 uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           mcu_i2c_memory_read(&g_touch_i2c_context,
                               address_7bit,
                               register_address,
                               data,
                               length,
                               timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Implement logical touch-bus register write. */
bsp_status_t bsp_i2c_memory_write(board_i2c_role_t role,
                                  uint8_t address_7bit,
                                  uint8_t register_address,
                                  const uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           mcu_i2c_memory_write(&g_touch_i2c_context,
                                address_7bit,
                                register_address,
                                data,
                                length,
                                timeout_ms) : BSP_STATUS_INVALID_ARGUMENT;
}
