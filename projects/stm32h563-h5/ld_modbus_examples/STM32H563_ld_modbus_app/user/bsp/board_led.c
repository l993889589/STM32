/**
 * @file board_led.c
 * @brief Status LED binding for the current board.
 */

#include "bsp_led.h"

#include <stdbool.h>

#include "board_config.h"
#include "stm32h5xx_hal.h"

static bool board_status_led_is_initialized;

/**
 * @brief Translate logical LED state to the board's active-low GPIO level.
 */
static GPIO_PinState board_status_led_level(bool is_on)
{
    const bool output_high = BOARD_STATUS_LED_ACTIVE_LOW != 0U ? !is_on : is_on;

    return output_high ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/**
 * @brief Implement bsp_led_init() as documented by its interface contract.
 */
bsp_status_t bsp_led_init(board_led_role_t role)
{
    GPIO_InitTypeDef gpio = {0};

    if(role != BOARD_LED_STATUS)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(board_status_led_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, board_status_led_level(false));
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);
    board_status_led_is_initialized = true;
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_led_set() as documented by its interface contract.
 */
bsp_status_t bsp_led_set(board_led_role_t role, bool is_on)
{
    if(role != BOARD_LED_STATUS)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(!board_status_led_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, board_status_led_level(is_on));
    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_led_toggle() as documented by its interface contract.
 */
bsp_status_t bsp_led_toggle(board_led_role_t role)
{
    if(role != BOARD_LED_STATUS)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    if(!board_status_led_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_12);
    return BSP_STATUS_OK;
}
