/**
 * @file bsp_stop.c
 * @brief STM32H563 safe GPIO state and visible unrecoverable-error loop.
 */

#include "bsp_stop.h"

#include "bsp_config.h"

static volatile bsp_stop_stage_t g_stop_stage = BSP_STOP_STAGE_NONE;

/** @brief Configure the active-low status LED without normal BSP dependencies. */
static void bsp_stop_led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(BOARD_STATUS_LED_PORT,
                      BOARD_STATUS_LED_PIN,
                      GPIO_PIN_SET);
    gpio.Pin = BOARD_STATUS_LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_STATUS_LED_PORT, &gpio);
}

/** @brief Delay approximately one visible blink interval without HAL tick use. */
static void bsp_stop_delay(void)
{
    volatile uint32_t cycles = SystemCoreClock / 8U;

    while(cycles != 0U)
    {
        cycles--;
        __NOP();
    }
}

/** @brief Enter the board safe-stop state and never return. */
void bsp_stop_on_error(bsp_stop_stage_t stage)
{
    __disable_irq();
    g_stop_stage = stage;
    bsp_stop_led_init();

    for(;;)
    {
        HAL_GPIO_TogglePin(BOARD_STATUS_LED_PORT, BOARD_STATUS_LED_PIN);
        bsp_stop_delay();
    }
}

/** @brief Return the RAM-resident safe-stop stage for debugger diagnostics. */
bsp_stop_stage_t bsp_stop_get_stage(void)
{
    return g_stop_stage;
}
