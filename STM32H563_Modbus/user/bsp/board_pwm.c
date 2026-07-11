/**
 * @file board_pwm.c
 * @brief Logical board PWM roles and STM32H5 PWM binding.
 */

#include "bsp_pwm.h"

#include "bsp_pwm_stm32h5.h"
#include "stm32h5xx_hal.h"

static const bsp_pwm_stm32h5_descriptor_t board_backlight_descriptor =
{
    .instance = TIM2,
    .channel = TIM_CHANNEL_4,
    .clock_id = BSP_CLOCK_TIMER_APB1,
    .counter_width_bits = 32U,
    .active_low = false
};

static bsp_pwm_stm32h5_context_t board_backlight_context;

/**
 * @brief Resolve a logical PWM role to its statically owned STM32H5 context.
 */
static bsp_pwm_stm32h5_context_t *board_pwm_context(board_pwm_role_t role)
{
    return role == BOARD_PWM_LCD_BACKLIGHT ? &board_backlight_context : NULL;
}

/**
 * @brief Implement bsp_pwm_init() as documented by its interface contract.
 */
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
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOB, &gpio);

    return bsp_pwm_stm32h5_init(&board_backlight_context,
                                &board_backlight_descriptor,
                                config,
                                result);
}

/**
 * @brief Implement bsp_pwm_configure() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_configure(board_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result)
{
    bsp_pwm_stm32h5_context_t *context = board_pwm_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_pwm_stm32h5_configure(context, config, result);
}

/**
 * @brief Implement bsp_pwm_start() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_start(board_pwm_role_t role)
{
    bsp_pwm_stm32h5_context_t *context = board_pwm_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT : bsp_pwm_stm32h5_start(context);
}

/**
 * @brief Implement bsp_pwm_stop() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_stop(board_pwm_role_t role)
{
    bsp_pwm_stm32h5_context_t *context = board_pwm_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT : bsp_pwm_stm32h5_stop(context);
}

/**
 * @brief Implement bsp_pwm_get_result() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_get_result(board_pwm_role_t role, bsp_pwm_result_t *result)
{
    bsp_pwm_stm32h5_context_t *context = board_pwm_context(role);
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
