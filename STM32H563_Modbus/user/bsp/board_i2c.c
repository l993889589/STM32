/**
 * @file board_i2c.c
 * @brief I2C1 touch-bus board binding.
 */

#include "bsp_i2c.h"

#include "bsp_clock.h"
#include "bsp_i2c_stm32h5.h"
#include "stm32h5xx_hal.h"

static bsp_i2c_stm32h5_context_t board_i2c_touch_context;

/**
 * @brief Implement bsp_i2c_init() as documented by its interface contract.
 */
bsp_status_t bsp_i2c_init(board_i2c_role_t role,
                          const bsp_i2c_config_t *config)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    uint32_t kernel_clock_hz;
    bsp_status_t status;

    if(role != BOARD_I2C_TOUCH)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_clock_get_hz(BSP_CLOCK_PCLK1, &kernel_clock_hz);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    peripheral_clock.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    return bsp_i2c_stm32h5_init(&board_i2c_touch_context,
                                I2C1,
                                kernel_clock_hz,
                                config);
}

/** @brief Implement bsp_i2c_get_achieved_bitrate() for the touch bus. */
bsp_status_t bsp_i2c_get_achieved_bitrate(
    board_i2c_role_t role,
    uint32_t *achieved_bitrate_hz)
{
    if((role != BOARD_I2C_TOUCH) || (achieved_bitrate_hz == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_i2c_touch_context.is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *achieved_bitrate_hz = board_i2c_touch_context.achieved_bitrate_hz;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_i2c_write() as documented by its interface contract.
 */
bsp_status_t bsp_i2c_write(board_i2c_role_t role,
                           uint8_t address_7bit,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           bsp_i2c_stm32h5_write(&board_i2c_touch_context,
                                 address_7bit,
                                 data,
                                 length,
                                 timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/**
 * @brief Implement bsp_i2c_read() as documented by its interface contract.
 */
bsp_status_t bsp_i2c_read(board_i2c_role_t role,
                          uint8_t address_7bit,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           bsp_i2c_stm32h5_read(&board_i2c_touch_context,
                                address_7bit,
                                data,
                                length,
                                timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/**
 * @brief Implement bsp_i2c_memory_read() as documented by its interface contract.
 */
bsp_status_t bsp_i2c_memory_read(board_i2c_role_t role,
                                 uint8_t address_7bit,
                                 uint8_t register_address,
                                 uint8_t *data,
                                 uint32_t length,
                                 uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           bsp_i2c_stm32h5_memory_read(&board_i2c_touch_context,
                                       address_7bit,
                                       register_address,
                                       data,
                                       length,
                                       timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}

/**
 * @brief Implement bsp_i2c_memory_write() as documented by its interface contract.
 */
bsp_status_t bsp_i2c_memory_write(board_i2c_role_t role,
                                  uint8_t address_7bit,
                                  uint8_t register_address,
                                  const uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms)
{
    return role == BOARD_I2C_TOUCH ?
           bsp_i2c_stm32h5_memory_write(&board_i2c_touch_context,
                                        address_7bit,
                                        register_address,
                                        data,
                                        length,
                                        timeout_ms) :
           BSP_STATUS_INVALID_ARGUMENT;
}
