/**
 * @file bsp_i2c.c
 * @brief PB6/PB7 open-drain software-I2C ownership for STM32F401CC.
 */

#include "bsp_i2c.h"

#include <stdbool.h>

#include "bsp_dwt.h"
#include "stm32f4xx_hal.h"

#define BSP_I2C_PORT          GPIOB
#define BSP_I2C_SCL_PIN       GPIO_PIN_6
#define BSP_I2C_SDA_PIN       GPIO_PIN_7
#define BSP_I2C_HALF_CYCLE_US (2U)

static bool g_i2c_initialized;
static volatile bool g_i2c_busy;

/** @brief Delay one bus half-cycle in physical microseconds. */
static void bsp_i2c_delay(void)
{
    bsp_dwt_delay_us(BSP_I2C_HALF_CYCLE_US);
}

/** @brief Release or pull low the open-drain clock line. */
static void bsp_i2c_set_scl(uint8_t high)
{
    BSP_I2C_PORT->BSRR = high != 0U ?
                         BSP_I2C_SCL_PIN :
                         ((uint32_t)BSP_I2C_SCL_PIN << 16U);
}

/** @brief Release or pull low the open-drain data line. */
static void bsp_i2c_set_sda(uint8_t high)
{
    BSP_I2C_PORT->BSRR = high != 0U ?
                         BSP_I2C_SDA_PIN :
                         ((uint32_t)BSP_I2C_SDA_PIN << 16U);
}

/** @brief Sample the released data line. */
static uint8_t bsp_i2c_get_sda(void)
{
    return (BSP_I2C_PORT->IDR & BSP_I2C_SDA_PIN) != 0U ? 1U : 0U;
}

/** @brief Generate one I2C start condition. */
static void bsp_i2c_start_condition(void)
{
    bsp_i2c_set_sda(1U);
    bsp_i2c_set_scl(1U);
    bsp_i2c_delay();
    bsp_i2c_set_sda(0U);
    bsp_i2c_delay();
    bsp_i2c_set_scl(0U);
}

/** @brief Generate one I2C stop condition. */
static void bsp_i2c_stop_condition(void)
{
    bsp_i2c_set_sda(0U);
    bsp_i2c_delay();
    bsp_i2c_set_scl(1U);
    bsp_i2c_delay();
    bsp_i2c_set_sda(1U);
    bsp_i2c_delay();
}

/** @brief Shift one byte MSB-first and return whether the slave acknowledged. */
static bool bsp_i2c_write_byte(uint8_t value)
{
    uint8_t bit;
    bool acknowledged;

    for(bit = 0U; bit < 8U; bit++)
    {
        bsp_i2c_set_sda((value & 0x80U) != 0U ? 1U : 0U);
        bsp_i2c_delay();
        bsp_i2c_set_scl(1U);
        bsp_i2c_delay();
        bsp_i2c_set_scl(0U);
        value <<= 1;
    }
    bsp_i2c_set_sda(1U);
    bsp_i2c_delay();
    bsp_i2c_set_scl(1U);
    bsp_i2c_delay();
    acknowledged = bsp_i2c_get_sda() == 0U;
    bsp_i2c_set_scl(0U);
    return acknowledged;
}

/** @brief Shift one byte in and send ACK for more data or NACK for the end. */
static uint8_t bsp_i2c_read_byte(bool acknowledge)
{
    uint8_t value = 0U;
    uint8_t bit;

    bsp_i2c_set_sda(1U);
    for(bit = 0U; bit < 8U; bit++)
    {
        value <<= 1;
        bsp_i2c_set_scl(1U);
        bsp_i2c_delay();
        value |= bsp_i2c_get_sda();
        bsp_i2c_set_scl(0U);
        bsp_i2c_delay();
    }
    bsp_i2c_set_sda(acknowledge ? 0U : 1U);
    bsp_i2c_delay();
    bsp_i2c_set_scl(1U);
    bsp_i2c_delay();
    bsp_i2c_set_scl(0U);
    bsp_i2c_set_sda(1U);
    return value;
}

/** @brief Claim the non-reentrant software bus with an IRQ-safe test-and-set. */
static bool bsp_i2c_enter(void)
{
    uint32_t state = __get_PRIMASK();
    bool acquired;

    __disable_irq();
    acquired = !g_i2c_busy;
    if(acquired)
        g_i2c_busy = true;
    __set_PRIMASK(state);
    return acquired;
}

/** @brief Release the software bus after a complete stop condition. */
static void bsp_i2c_exit(void)
{
    g_i2c_busy = false;
}

/** @brief Initialize both open-drain pins and emit a bus recovery stop. */
bsp_status_t bsp_i2c_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t pulse;

    if(g_i2c_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = BSP_I2C_SCL_PIN | BSP_I2C_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_I2C_PORT, &gpio);
    bsp_i2c_set_sda(1U);
    for(pulse = 0U; pulse < 9U; pulse++)
    {
        bsp_i2c_set_scl(0U);
        bsp_i2c_delay();
        bsp_i2c_set_scl(1U);
        bsp_i2c_delay();
    }
    bsp_i2c_stop_condition();
    g_i2c_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Write one transaction to a seven-bit slave. */
bsp_status_t bsp_i2c_write(uint8_t address_7bit,
                           const uint8_t *data,
                           size_t length)
{
    size_t index;
    bsp_status_t status = BSP_STATUS_OK;

    if(address_7bit > 0x7FU || (data == NULL && length != 0U))
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_i2c_initialized)
        return BSP_STATUS_NOT_READY;
    if(!bsp_i2c_enter())
        return BSP_STATUS_BUSY;

    bsp_i2c_start_condition();
    if(!bsp_i2c_write_byte((uint8_t)(address_7bit << 1)))
        status = BSP_STATUS_IO_ERROR;
    for(index = 0U; status == BSP_STATUS_OK && index < length; index++)
    {
        if(!bsp_i2c_write_byte(data[index]))
            status = BSP_STATUS_IO_ERROR;
    }
    bsp_i2c_stop_condition();
    bsp_i2c_exit();
    return status;
}

/** @brief Read one transaction from a seven-bit slave. */
bsp_status_t bsp_i2c_read(uint8_t address_7bit,
                          uint8_t *data,
                          size_t length)
{
    size_t index;

    if(address_7bit > 0x7FU || data == NULL || length == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_i2c_initialized)
        return BSP_STATUS_NOT_READY;
    if(!bsp_i2c_enter())
        return BSP_STATUS_BUSY;

    bsp_i2c_start_condition();
    if(!bsp_i2c_write_byte((uint8_t)((address_7bit << 1) | 1U)))
    {
        bsp_i2c_stop_condition();
        bsp_i2c_exit();
        return BSP_STATUS_IO_ERROR;
    }
    for(index = 0U; index < length; index++)
        data[index] = bsp_i2c_read_byte(index + 1U < length);
    bsp_i2c_stop_condition();
    bsp_i2c_exit();
    return BSP_STATUS_OK;
}

/** @brief Probe a slave by issuing an address-only write transaction. */
bsp_status_t bsp_i2c_probe(uint8_t address_7bit)
{
    return bsp_i2c_write(address_7bit, NULL, 0U);
}
