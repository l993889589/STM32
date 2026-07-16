/**
 * @file bsp_gpio.c
 * @brief CHPM status and control GPIO initialization.
 */

#include "bsp_gpio.h"

#include <stdbool.h>

#include "board_config.h"

static bool gpio_initialized;

/** @brief Configure status and control pins to deterministic safe levels. */
bsp_status_t bsp_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if(gpio_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    HAL_GPIO_WritePin(BOARD_STATUS_GPIO_PORT,
                      BOARD_STATUS_GPIO_PIN,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BOARD_CONTROL_GPIO_PORT,
                      BOARD_CONTROL_GPIO_PINS,
                      GPIO_PIN_RESET);

    gpio.Pin = BOARD_STATUS_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_STATUS_GPIO_PORT, &gpio);

    gpio.Pin = BOARD_CONTROL_GPIO_PINS;
    HAL_GPIO_Init(BOARD_CONTROL_GPIO_PORT, &gpio);
    gpio_initialized = true;
    return BSP_STATUS_OK;
}
