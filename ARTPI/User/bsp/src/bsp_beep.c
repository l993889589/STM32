#include "bsp.h"

/* INDUSTRY-IO active buzzer: PH7 drives the base of an NPN transistor. */
#define BSP_BEEP_PORT GPIOH
#define BSP_BEEP_PIN  GPIO_PIN_7

static uint8_t beep_is_on;

void bsp_beep_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* Keep the active buzzer silent before changing the GPIO mode. */
    HAL_GPIO_WritePin(BSP_BEEP_PORT, BSP_BEEP_PIN, GPIO_PIN_RESET);

    gpio_config.Pin = BSP_BEEP_PIN;
    gpio_config.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_BEEP_PORT, &gpio_config);

    beep_is_on = 0U;
}

void bsp_beep_on(void)
{
    HAL_GPIO_WritePin(BSP_BEEP_PORT, BSP_BEEP_PIN, GPIO_PIN_SET);
    beep_is_on = 1U;
}

void bsp_beep_off(void)
{
    HAL_GPIO_WritePin(BSP_BEEP_PORT, BSP_BEEP_PIN, GPIO_PIN_RESET);
    beep_is_on = 0U;
}

void bsp_beep_toggle(void)
{
    if (beep_is_on != 0U)
    {
        bsp_beep_off();
    }
    else
    {
        bsp_beep_on();
    }
}

uint8_t bsp_beep_is_on(void)
{
    return beep_is_on;
}
