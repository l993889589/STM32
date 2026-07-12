/**
 * @file bsp_pwm.c
 * @brief LCD backlight PWM board binding.
 */

#include "bsp_pwm.h"

#include <stddef.h>

#include "board_resources.h"
#include "mcu_pwm.h"

static const mcu_pwm_descriptor_t g_lcd_backlight_descriptor =
{
    .instance = BOARD_PWM_LCD_INSTANCE,
    .channel = BOARD_PWM_LCD_CHANNEL,
    .clock_id = BSP_CLOCK_TIMER_APB1,
    .counter_width_bits = 32U,
    .active_low = BOARD_PWM_LCD_ACTIVE_LOW != 0U
};

static mcu_pwm_context_t g_lcd_backlight_context;

/** @brief Resolve one logical PWM role to its static STM32 context. */
static mcu_pwm_context_t *board_pwm_get_context(board_pwm_role_t role)
{
    return role == BOARD_PWM_LCD_BACKLIGHT ? &g_lcd_backlight_context : NULL;
}

/** @brief Implement bsp_pwm_init() for the PB11 TIM2_CH4 binding. */
bsp_status_t bsp_pwm_init(board_pwm_role_t role,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result)
{
    GPIO_InitTypeDef gpio = {0};

    if(role != BOARD_PWM_LCD_BACKLIGHT)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    gpio.Pin = BOARD_PWM_LCD_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = BOARD_PWM_LCD_AF;
    HAL_GPIO_Init(BOARD_PWM_LCD_PORT, &gpio);

    return mcu_pwm_init(&g_lcd_backlight_context,
                        &g_lcd_backlight_descriptor,
                        config,
                        result);
}

/** @brief Implement bsp_pwm_configure() through the role-owned context. */
bsp_status_t bsp_pwm_configure(board_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result)
{
    mcu_pwm_context_t *context = board_pwm_get_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT :
           mcu_pwm_configure(context, config, result);
}

/** @brief Implement bsp_pwm_start() through the role-owned context. */
bsp_status_t bsp_pwm_start(board_pwm_role_t role)
{
    mcu_pwm_context_t *context = board_pwm_get_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT : mcu_pwm_start(context);
}

/** @brief Implement bsp_pwm_stop() through the role-owned context. */
bsp_status_t bsp_pwm_stop(board_pwm_role_t role)
{
    mcu_pwm_context_t *context = board_pwm_get_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT : mcu_pwm_stop(context);
}

/** @brief Implement bsp_pwm_get_result() through the role-owned context. */
bsp_status_t bsp_pwm_get_result(board_pwm_role_t role, bsp_pwm_result_t *result)
{
    mcu_pwm_context_t *context = board_pwm_get_context(role);

    if((context == NULL) || (result == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *result = context->result;
    return BSP_STATUS_OK;
}
