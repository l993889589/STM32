/**
 * @file bsp_led.c
 * @brief Direct ownership of the active-low PC13 status LED.
 */

#include "bsp_led.h"

#include "stm32f4xx_hal.h"

#define BSP_LED_STATUS_PORT GPIOC
#define BSP_LED_STATUS_PIN  GPIO_PIN_13

/** @brief Configure PC13 as an inactive active-low LED output. */
void bsp_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN, GPIO_PIN_SET);
    gpio.Pin = BSP_LED_STATUS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_LED_STATUS_PORT, &gpio);
}

/** @brief Drive the active-low status LED on. */
void bsp_led_on(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
        HAL_GPIO_WritePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN,
                          GPIO_PIN_RESET);
}

/** @brief Drive the active-low status LED off. */
void bsp_led_off(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
        HAL_GPIO_WritePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN,
                          GPIO_PIN_SET);
}

/** @brief Toggle the active-low status LED. */
void bsp_led_toggle(bsp_led_t led)
{
    if(led == BSP_LED_STATUS)
        HAL_GPIO_TogglePin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN);
}

/** @brief Read the logical status LED state. */
uint8_t bsp_led_is_on(bsp_led_t led)
{
    if(led != BSP_LED_STATUS)
        return 0U;
    return HAL_GPIO_ReadPin(BSP_LED_STATUS_PORT, BSP_LED_STATUS_PIN) ==
           GPIO_PIN_RESET ? 1U : 0U;
}
