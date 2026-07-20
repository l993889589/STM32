/**
 * @file bsp_pwm.c
 * @brief PA8/TIM1_CH1 fan PWM ownership for STM32F401CC.
 */

#include "bsp_pwm.h"

#include <stdbool.h>
#include <stddef.h>

#include "bsp_clock.h"
#include "stm32f4xx_hal.h"

#define BSP_PWM_INSTANCE TIM1
#define BSP_PWM_CHANNEL  TIM_CHANNEL_1
#define BSP_PWM_PORT     GPIOA
#define BSP_PWM_PIN      GPIO_PIN_8
#define BSP_PWM_AF       GPIO_AF1_TIM1

typedef struct
{
    uint32_t prescaler;
    uint32_t auto_reload;
    uint32_t achieved_frequency_hz;
    uint32_t error_ppm;
} bsp_pwm_solution_t;

static TIM_HandleTypeDef g_fan_timer;
static bsp_pwm_result_t g_fan_result;
static bool g_fan_initialized;

/** @brief Find the best 16-bit TIM1 divider while maximizing resolution. */
static bsp_status_t bsp_pwm_solve(uint32_t timer_clock_hz,
                                  uint32_t requested_frequency_hz,
                                  bsp_pwm_solution_t *solution)
{
    uint64_t best_error = UINT64_MAX;
    uint32_t best_counts = 0U;
    uint32_t prescaler;
    bool found = false;

    if(timer_clock_hz == 0U || requested_frequency_hz == 0U ||
       solution == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;

    for(prescaler = 0U; prescaler <= UINT16_MAX; prescaler++)
    {
        uint64_t divider = (uint64_t)requested_frequency_hz *
                           (prescaler + 1ULL);
        uint64_t counts = ((uint64_t)timer_clock_hz + divider / 2ULL) /
                          divider;
        uint64_t achieved;
        uint64_t error;

        if(counts == 0U || counts > 65536ULL)
            continue;
        achieved = (uint64_t)timer_clock_hz /
                   ((prescaler + 1ULL) * counts);
        error = (achieved > requested_frequency_hz) ?
                (achieved - requested_frequency_hz) :
                (requested_frequency_hz - achieved);
        if(error < best_error ||
           (error == best_error && counts > best_counts))
        {
            best_error = error;
            best_counts = (uint32_t)counts;
            solution->prescaler = prescaler;
            solution->auto_reload = (uint32_t)counts - 1U;
            solution->achieved_frequency_hz = (uint32_t)achieved;
            found = true;
            if(error == 0U && prescaler == 0U)
                break;
        }
    }
    if(!found)
        return BSP_STATUS_NOT_SUPPORTED;
    solution->error_ppm =
        (uint32_t)((best_error * 1000000ULL) / requested_frequency_hz);
    return BSP_STATUS_OK;
}

/** @brief Apply one frequency and duty solution to TIM1 channel 1. */
static bsp_status_t bsp_pwm_apply(const bsp_pwm_config_t *config,
                                  bsp_pwm_result_t *result)
{
    bsp_pwm_solution_t solution;
    TIM_OC_InitTypeDef output = {0};
    bsp_status_t status;
    uint32_t timer_clock_hz;
    uint64_t counts;
    uint64_t compare;

    if(config == NULL || config->duty_permyriad > 10000U)
        return BSP_STATUS_INVALID_ARGUMENT;
    status = bsp_clock_get_hz(BSP_CLOCK_TIMER_APB2, &timer_clock_hz);
    if(status != BSP_STATUS_OK)
        return status;
    status = bsp_pwm_solve(timer_clock_hz, config->frequency_hz, &solution);
    if(status != BSP_STATUS_OK)
        return status;

    g_fan_timer.Instance = BSP_PWM_INSTANCE;
    g_fan_timer.Init.Prescaler = solution.prescaler;
    g_fan_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_fan_timer.Init.Period = solution.auto_reload;
    g_fan_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_fan_timer.Init.RepetitionCounter = 0U;
    g_fan_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if(HAL_TIM_PWM_Init(&g_fan_timer) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    counts = (uint64_t)solution.auto_reload + 1ULL;
    compare = (counts * config->duty_permyriad + 5000ULL) / 10000ULL;
    output.OCMode = (config->duty_permyriad == 0U) ?
                    TIM_OCMODE_FORCED_INACTIVE :
                    ((config->duty_permyriad == 10000U) ?
                     TIM_OCMODE_FORCED_ACTIVE : TIM_OCMODE_PWM1);
    output.Pulse = (uint32_t)compare;
    output.OCPolarity = TIM_OCPOLARITY_HIGH;
    output.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;
    output.OCIdleState = TIM_OCIDLESTATE_RESET;
    output.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if(HAL_TIM_PWM_ConfigChannel(&g_fan_timer,
                                 &output,
                                 BSP_PWM_CHANNEL) != HAL_OK)
        return BSP_STATUS_IO_ERROR;
    if(HAL_TIM_PWM_Start(&g_fan_timer, BSP_PWM_CHANNEL) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    g_fan_result.requested_frequency_hz = config->frequency_hz;
    g_fan_result.achieved_frequency_hz = solution.achieved_frequency_hz;
    g_fan_result.error_ppm = solution.error_ppm;
    g_fan_result.timer_clock_hz = timer_clock_hz;
    g_fan_result.prescaler = solution.prescaler;
    g_fan_result.auto_reload = solution.auto_reload;
    g_fan_result.compare = (uint32_t)compare;
    g_fan_result.requested_duty_permyriad = config->duty_permyriad;
    if(result != NULL)
        *result = g_fan_result;
    return BSP_STATUS_OK;
}

/** @brief Claim PA8 and TIM1 before applying the initial request. */
bsp_status_t bsp_pwm_init(bsp_pwm_role_t role,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result)
{
    GPIO_InitTypeDef gpio = {0};
    bsp_status_t status;

    if(role != BSP_PWM_FAN || config == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(g_fan_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    gpio.Pin = BSP_PWM_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = BSP_PWM_AF;
    HAL_GPIO_Init(BSP_PWM_PORT, &gpio);
    status = bsp_pwm_apply(config, result);
    if(status == BSP_STATUS_OK)
        g_fan_initialized = true;
    return status;
}

/** @brief Re-solve and apply the fan PWM without exposing TIM1. */
bsp_status_t bsp_pwm_configure(bsp_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result)
{
    if(role != BSP_PWM_FAN || config == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_fan_initialized)
        return BSP_STATUS_NOT_READY;
    return bsp_pwm_apply(config, result);
}

/** @brief Stop TIM1 channel 1 after validating ownership. */
bsp_status_t bsp_pwm_stop(bsp_pwm_role_t role)
{
    if(role != BSP_PWM_FAN)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_fan_initialized)
        return BSP_STATUS_NOT_READY;
    return (HAL_TIM_PWM_Stop(&g_fan_timer, BSP_PWM_CHANNEL) == HAL_OK) ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Copy the last achieved physical settings. */
bsp_status_t bsp_pwm_get_result(bsp_pwm_role_t role,
                                bsp_pwm_result_t *result)
{
    if(role != BSP_PWM_FAN || result == NULL)
        return BSP_STATUS_INVALID_ARGUMENT;
    if(!g_fan_initialized)
        return BSP_STATUS_NOT_READY;
    *result = g_fan_result;
    return BSP_STATUS_OK;
}
