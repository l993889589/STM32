/**
 * @file board_init.c
 * @brief Ordered board initialization and early safe-output setup.
 */

#include "board_init.h"

#include <stdbool.h>

#include "board_control.h"
#include "board_config.h"
#include "bsp_health.h"
#include "bsp_led.h"
#include "bsp_target.h"
#include "stm32h5xx_hal.h"

const bsp_clock_config_t board_clock_config =
{
    .hse_frequency_hz = BOARD_HSE_FREQUENCY_HZ,
    .pll1_m = 2U,
    .pll1_n = 40U,
    .pll1_p = 2U,
    .pll1_q = 2U,
    .pll1_r = 2U,
    .expected_sysclk_hz = BOARD_EXPECTED_SYSCLK_HZ
};

static bool board_is_initialized;

/**
 * @brief Configure one GPIO output while writing its safe level before mode activation.
 */
static void board_configure_safe_output(GPIO_TypeDef *port,
                                        uint32_t pin,
                                        GPIO_PinState safe_level)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_WritePin(port, pin, safe_level);
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}

/**
 * @brief Implement board_early_safe_gpio_init() as documented by its interface contract.
 */
bsp_status_t board_early_safe_gpio_init(void)
{
    bsp_status_t status;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    board_configure_safe_output(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    board_configure_safe_output(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
    board_configure_safe_output(GPIOB, GPIO_PIN_11,
                                BOARD_LCD_BACKLIGHT_SAFE_LEVEL != 0U ?
                                GPIO_PIN_SET : GPIO_PIN_RESET);
    board_configure_safe_output(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
    board_configure_safe_output(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);

    status = board_control_init();
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }
    return BSP_STATUS_OK;
}

/**
 * @brief Implement board_init() as documented by its interface contract.
 */
bsp_status_t board_init(void)
{
    bsp_status_t status;
    uint32_t reset_flags;

    if(board_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    reset_flags = RCC->RSR;
    bsp_health_init(reset_flags);
    __HAL_RCC_CLEAR_RESET_FLAGS();

    status = board_early_safe_gpio_init();
    if(status != BSP_STATUS_OK)
    {
        bsp_health_record_error(BSP_ERROR_STAGE_SAFE_GPIO, status);
        return status;
    }

    status = bsp_clock_init(&board_clock_config);
    if(status != BSP_STATUS_OK)
    {
        bsp_health_record_error(BSP_ERROR_STAGE_CLOCK, status);
        return status;
    }

    status = bsp_led_init(BOARD_LED_STATUS);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        bsp_health_record_error(BSP_ERROR_STAGE_BOARD, status);
        return status;
    }

    board_is_initialized = true;
    return BSP_STATUS_OK;
}
