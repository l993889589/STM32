/**
 * @file bsp_control.c
 * @brief Direct ownership and safe initialization of PB14 and PB15.
 */

#include "bsp_control.h"

#include <stdbool.h>

#include "stm32f4xx_hal.h"

#define BSP_CONTROL_PORT GPIOB
#define BSP_CONTROL_PINS (GPIO_PIN_14 | GPIO_PIN_15)

static bool control_initialized;

/** @brief Configure PB14/PB15 as low push-pull outputs. */
bsp_status_t bsp_control_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if(control_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(BSP_CONTROL_PORT, BSP_CONTROL_PINS, GPIO_PIN_RESET);
    gpio.Pin = BSP_CONTROL_PINS;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_CONTROL_PORT, &gpio);
    control_initialized = true;
    return BSP_STATUS_OK;
}
