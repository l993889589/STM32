#include "bsp.h"

#define BSP_TIM_PWM_INSTANCE  TIM5
#define BSP_TIM_PWM_CHANNEL   TIM_CHANNEL_1
#define BSP_TIM_PWM_GPIO_PORT GPIOH
#define BSP_TIM_PWM_GPIO_PIN  GPIO_PIN_10
#define BSP_TIM_PWM_GPIO_AF   GPIO_AF2_TIM5

static TIM_HandleTypeDef tim_pwm_handle;
static uint8_t tim_pwm_initialized;

static uint32_t bsp_tim_pwm_get_clock(void);

HAL_StatusTypeDef bsp_tim_pwm_init(void)
{
    GPIO_InitTypeDef gpio_config = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();

    gpio_config.Pin = BSP_TIM_PWM_GPIO_PIN;
    gpio_config.Mode = GPIO_MODE_AF_PP;
    gpio_config.Pull = GPIO_NOPULL;
    gpio_config.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_config.Alternate = BSP_TIM_PWM_GPIO_AF;
    HAL_GPIO_Init(BSP_TIM_PWM_GPIO_PORT, &gpio_config);

    tim_pwm_handle.Instance = BSP_TIM_PWM_INSTANCE;
    tim_pwm_initialized = 1U;
    return bsp_tim_pwm_stop();
}

HAL_StatusTypeDef bsp_tim_pwm_set(uint32_t frequency_hz,
                                  uint16_t duty_permyriad)
{
    TIM_OC_InitTypeDef output_compare = {0};
    uint32_t timer_clock;
    uint32_t period_ticks;
    uint32_t pulse_ticks;

    if ((tim_pwm_initialized == 0U) || (frequency_hz == 0U) ||
        (duty_permyriad > 10000U))
    {
        return HAL_ERROR;
    }

    timer_clock = bsp_tim_pwm_get_clock();
    if (frequency_hz > timer_clock)
    {
        return HAL_ERROR;
    }

    period_ticks = timer_clock / frequency_hz;
    if (period_ticks == 0U)
    {
        return HAL_ERROR;
    }

    pulse_ticks = (uint32_t)(((uint64_t)period_ticks * duty_permyriad) / 10000U);

    CLEAR_BIT(BSP_TIM_PWM_INSTANCE->CR1, TIM_CR1_CEN);
    tim_pwm_handle.Init.Prescaler = 0U;
    tim_pwm_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    tim_pwm_handle.Init.Period = period_ticks - 1U;
    tim_pwm_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    tim_pwm_handle.Init.RepetitionCounter = 0U;
    tim_pwm_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&tim_pwm_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    output_compare.OCMode = TIM_OCMODE_PWM1;
    output_compare.Pulse = pulse_ticks;
    output_compare.OCPolarity = TIM_OCPOLARITY_HIGH;
    output_compare.OCFastMode = TIM_OCFAST_DISABLE;
    output_compare.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    output_compare.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    output_compare.OCIdleState = TIM_OCIDLESTATE_RESET;

    if (HAL_TIM_PWM_ConfigChannel(&tim_pwm_handle,
                                  &output_compare,
                                  BSP_TIM_PWM_CHANNEL) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_TIM_PWM_Start(&tim_pwm_handle, BSP_TIM_PWM_CHANNEL);
}

HAL_StatusTypeDef bsp_tim_pwm_stop(void)
{
    if (tim_pwm_initialized == 0U)
    {
        return HAL_ERROR;
    }

    CLEAR_BIT(BSP_TIM_PWM_INSTANCE->CCER, TIM_CCER_CC1E);
    CLEAR_BIT(BSP_TIM_PWM_INSTANCE->CR1, TIM_CR1_CEN);
    WRITE_REG(BSP_TIM_PWM_INSTANCE->CCR1, 0U);
    return HAL_OK;
}

static uint32_t bsp_tim_pwm_get_clock(void)
{
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();

    if ((RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) != RCC_D2CFGR_D2PPRE1_DIV1)
    {
        timer_clock *= 2U;
    }

    return timer_clock;
}
