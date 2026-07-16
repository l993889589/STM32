/**
 * @file bsp_led.c
 * @brief ART-Pi H750 LED BSP implementation.
 */

#include "bsp.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} bsp_led_config_t;

static const bsp_led_config_t led_config[BSP_LED_COUNT] =
{
    {GPIOI, GPIO_PIN_8},
    {GPIOC, GPIO_PIN_15}
};

/** @brief Perform the bsp_led_is_valid board-support operation. */
static uint8_t bsp_led_is_valid(bsp_led_t led)
{
    return ((uint32_t)led < (uint32_t)BSP_LED_COUNT) ? 1U : 0U;
}

/** @brief Perform the bsp_led_init board-support operation. */
void bsp_led_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);

    gpio_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_LOW;

    gpio_config.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOI, &gpio_config);

    gpio_config.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOC, &gpio_config);
}

/** @brief Perform the bsp_led_on board-support operation. */
void bsp_led_on(bsp_led_t led)
{
    if (bsp_led_is_valid(led) != 0U)
    {
        HAL_GPIO_WritePin(led_config[led].port, led_config[led].pin, GPIO_PIN_RESET);
    }
}

/** @brief Perform the bsp_led_off board-support operation. */
void bsp_led_off(bsp_led_t led)
{
    if (bsp_led_is_valid(led) != 0U)
    {
        HAL_GPIO_WritePin(led_config[led].port, led_config[led].pin, GPIO_PIN_SET);
    }
}

/** @brief Perform the bsp_led_toggle board-support operation. */
void bsp_led_toggle(bsp_led_t led)
{
    if (bsp_led_is_valid(led) != 0U)
    {
        HAL_GPIO_TogglePin(led_config[led].port, led_config[led].pin);
    }
}

/** @brief Perform the bsp_led_is_on board-support operation. */
uint8_t bsp_led_is_on(bsp_led_t led)
{
    if (bsp_led_is_valid(led) == 0U)
    {
        return 0U;
    }

    return (HAL_GPIO_ReadPin(led_config[led].port, led_config[led].pin) == GPIO_PIN_RESET) ? 1U : 0U;
}
