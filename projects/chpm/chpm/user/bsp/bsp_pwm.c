/**
 * @file bsp_pwm.c
 * @brief Fan PWM generation with validated frequency and duty inputs.
 */

#include "bsp_pwm.h"

#include "board_config.h"

#include <stdbool.h>

static TIM_HandleTypeDef s_pwm;
static bool s_pwm_started;

/** @brief Return the effective TIM1 input clock including APB multiplication. */
static uint32_t fan_timer_clock_hz(void)
{
    uint32_t clock = HAL_RCC_GetPCLK2Freq();
    uint32_t prescaler_bits = RCC->CFGR & RCC_CFGR_PPRE2;

    if(prescaler_bits != RCC_CFGR_PPRE2_DIV1)
        clock *= 2U;
    return clock;
}

/** @brief Stop PWM and drive the fan pin as a fixed digital output. */
static void fan_gpio_output(GPIO_PinState state)
{
    GPIO_InitTypeDef gpio = {0};

    if(s_pwm_started)
    {
        (void)HAL_TIM_PWM_Stop(&s_pwm, BOARD_FAN_PWM_CHANNEL);
        s_pwm_started = false;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = BOARD_FAN_PWM_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_FAN_PWM_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(BOARD_FAN_PWM_GPIO_PORT, BOARD_FAN_PWM_GPIO_PIN, state);
}

/** @brief Configure fan PWM in physical frequency and permyriad duty units. */
bsp_status_t bsp_fan_pwm_set(uint32_t frequency_hz, uint16_t duty_permyriad)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef output = {0};
    uint32_t timer_clock;
    uint32_t prescaler;
    uint32_t counts;
    uint32_t pulse;

    if(frequency_hz == 0U || duty_permyriad > 10000U)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(duty_permyriad == 0U)
    {
        fan_gpio_output(GPIO_PIN_RESET);
        return BSP_STATUS_OK;
    }
    if(duty_permyriad == 10000U)
    {
        fan_gpio_output(GPIO_PIN_SET);
        return BSP_STATUS_OK;
    }

    timer_clock = fan_timer_clock_hz();
    if(timer_clock == 0U || frequency_hz > timer_clock)
        return BSP_STATUS_INVALID_ARGUMENT;
    prescaler = (uint32_t)(((uint64_t)timer_clock +
                 (uint64_t)frequency_hz * 65536ULL - 1ULL) /
                ((uint64_t)frequency_hz * 65536ULL));
    if(prescaler != 0U)
        prescaler--;
    counts = timer_clock / ((prescaler + 1U) * frequency_hz);
    if(counts < 2U || counts > 65536U || prescaler > 65535U)
        return BSP_STATUS_INVALID_ARGUMENT;
    pulse = (counts * duty_permyriad + 5000U) / 10000U;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    gpio.Pin = BOARD_FAN_PWM_GPIO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = BOARD_FAN_PWM_GPIO_AF;
    HAL_GPIO_Init(BOARD_FAN_PWM_GPIO_PORT, &gpio);

    if(s_pwm_started)
        (void)HAL_TIM_PWM_Stop(&s_pwm, BOARD_FAN_PWM_CHANNEL);
    s_pwm.Instance = BOARD_FAN_PWM_TIMER;
    s_pwm.Init.Prescaler = prescaler;
    s_pwm.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_pwm.Init.Period = counts - 1U;
    s_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_pwm.Init.RepetitionCounter = 0U;
    s_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if(HAL_TIM_PWM_Init(&s_pwm) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    output.OCMode = TIM_OCMODE_PWM1;
    output.Pulse = pulse;
    output.OCPolarity = TIM_OCPOLARITY_HIGH;
    output.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;
    output.OCIdleState = TIM_OCIDLESTATE_RESET;
    output.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if(HAL_TIM_PWM_ConfigChannel(&s_pwm, &output, BOARD_FAN_PWM_CHANNEL) != HAL_OK ||
       HAL_TIM_PWM_Start(&s_pwm, BOARD_FAN_PWM_CHANNEL) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
    s_pwm_started = true;
    return BSP_STATUS_OK;
}

/** @brief Preserve the validated legacy fan PWM call signature. */
void bsp_SetTIMOutPWM(GPIO_TypeDef *gpio, uint16_t pin, TIM_TypeDef *timer,
                      uint8_t channel, uint32_t frequency_hz,
                      uint32_t duty_permyriad)
{
    if(gpio != BOARD_FAN_PWM_GPIO_PORT || pin != BOARD_FAN_PWM_GPIO_PIN ||
       timer != BOARD_FAN_PWM_TIMER || channel != 1U || duty_permyriad > 10000U)
        return;
    (void)bsp_fan_pwm_set(frequency_hz, (uint16_t)duty_permyriad);
}
