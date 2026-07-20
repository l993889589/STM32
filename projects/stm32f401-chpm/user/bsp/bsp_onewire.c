/**
 * @file bsp_onewire.c
 * @brief PB0 open-drain 1-Wire ownership using the BSP DWT timebase.
 */

#include "bsp_onewire.h"

#include <stdbool.h>

#include "bsp_dwt.h"
#include "stm32f4xx_hal.h"

#define BSP_ONEWIRE_PORT GPIOB
#define BSP_ONEWIRE_PIN  GPIO_PIN_0

static bool onewire_initialized;

/** @brief Drive PB0 as an open-drain 1-Wire output. */
static void bsp_onewire_set_output(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = BSP_ONEWIRE_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_ONEWIRE_PORT, &gpio);
}

/** @brief Release PB0 and sample it as a pulled-up input. */
static void bsp_onewire_set_input(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = BSP_ONEWIRE_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_ONEWIRE_PORT, &gpio);
}

/**
 * @brief Drive the 1-Wire line low or release it high.
 * @param high Nonzero releases the open-drain output.
 */
static void bsp_onewire_write_pin(uint8_t high)
{
    HAL_GPIO_WritePin(BSP_ONEWIRE_PORT,
                      BSP_ONEWIRE_PIN,
                      high != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief Sample the current 1-Wire line level.
 * @return One for a high line or zero for a low line.
 */
static uint8_t bsp_onewire_read_pin(void)
{
    return HAL_GPIO_ReadPin(BSP_ONEWIRE_PORT,
                            BSP_ONEWIRE_PIN) == GPIO_PIN_SET ? 1U : 0U;
}

/**
 * @brief Read one least-significant-bit-first 1-Wire time slot.
 * @return Sampled bit value.
 */
static uint8_t bsp_onewire_read_bit(void)
{
    uint8_t bit;

    bsp_onewire_set_output();
    bsp_onewire_write_pin(0U);
    bsp_dwt_delay_us(2U);
    bsp_onewire_write_pin(1U);
    bsp_onewire_set_input();
    bsp_dwt_delay_us(12U);
    bit = bsp_onewire_read_pin();
    bsp_dwt_delay_us(50U);
    return bit;
}

/**
 * @brief Read one byte from eight 1-Wire time slots.
 * @return Least-significant-bit-first byte value.
 */
static uint8_t bsp_onewire_read_byte(void)
{
    uint8_t value = 0U;
    uint8_t bit;

    for(bit = 0U; bit < 8U; bit++)
        value |= (uint8_t)(bsp_onewire_read_bit() << bit);
    return value;
}

/**
 * @brief Write one byte over eight 1-Wire time slots.
 * @param value Byte transmitted least-significant bit first.
 */
static void bsp_onewire_write_byte(uint8_t value)
{
    uint8_t bit;

    bsp_onewire_set_output();
    for(bit = 0U; bit < 8U; bit++)
    {
        bsp_onewire_write_pin(0U);
        if((value & 0x01U) != 0U)
        {
            bsp_dwt_delay_us(2U);
            bsp_onewire_write_pin(1U);
            bsp_dwt_delay_us(60U);
        }
        else
        {
            bsp_dwt_delay_us(60U);
            bsp_onewire_write_pin(1U);
            bsp_dwt_delay_us(2U);
        }
        value >>= 1;
    }
}

/**
 * @brief Initialize PB0 as the released open-drain 1-Wire bus.
 * @return BSP initialization status.
 */
bsp_status_t bsp_onewire_init(void)
{
    if(onewire_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    bsp_onewire_set_output();
    bsp_onewire_write_pin(1U);
    onewire_initialized = true;
    return BSP_STATUS_OK;
}

/**
 * @brief Generate a reset pulse and validate a device presence pulse.
 * @return OK when a presence pulse is detected, otherwise a typed status.
 */
bsp_status_t bsp_onewire_reset(void)
{
    uint16_t elapsed;

    if(!onewire_initialized)
        return BSP_STATUS_NOT_READY;

    bsp_onewire_set_output();
    bsp_onewire_write_pin(0U);
    bsp_dwt_delay_us(750U);
    bsp_onewire_write_pin(1U);
    bsp_dwt_delay_us(15U);
    bsp_onewire_set_input();

    for(elapsed = 0U;
        elapsed < 200U && bsp_onewire_read_pin() != 0U;
        elapsed++)
        bsp_dwt_delay_us(1U);
    if(elapsed == 200U)
        return BSP_STATUS_IO_ERROR;
    for(elapsed = 0U;
        elapsed < 240U && bsp_onewire_read_pin() == 0U;
        elapsed++)
        bsp_dwt_delay_us(1U);
    return elapsed < 240U ? BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/**
 * @brief Write bytes using least-significant-bit-first 1-Wire slots.
 * @param data Source bytes.
 * @param length Number of bytes to write.
 * @return Typed validation or readiness status.
 */
bsp_status_t bsp_onewire_write(const uint8_t *data, size_t length)
{
    size_t index;

    if(data == NULL && length != 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!onewire_initialized)
        return BSP_STATUS_NOT_READY;

    for(index = 0U; index < length; index++)
        bsp_onewire_write_byte(data[index]);
    return BSP_STATUS_OK;
}

/**
 * @brief Read bytes using least-significant-bit-first 1-Wire slots.
 * @param data Destination bytes.
 * @param length Number of bytes to read.
 * @return Typed validation or readiness status.
 */
bsp_status_t bsp_onewire_read(uint8_t *data, size_t length)
{
    size_t index;

    if(data == NULL || length == 0U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!onewire_initialized)
        return BSP_STATUS_NOT_READY;

    for(index = 0U; index < length; index++)
        data[index] = bsp_onewire_read_byte();
    return BSP_STATUS_OK;
}
