/**
 * @file bsp_pwm_stm32h5.c
 * @brief STM32H5 timer/PWM solver and runtime implementation.
 */

#include "bsp_pwm_stm32h5.h"

#include <limits.h>

typedef struct
{
    uint32_t prescaler;
    uint32_t auto_reload;
    uint32_t achieved_frequency_hz;
    uint32_t error_ppm;
} bsp_pwm_solution_t;

/**
 * @brief Solve prescaler, auto-reload, and compare values using wide arithmetic.
 */
static bsp_status_t bsp_pwm_solve(uint32_t timer_clock_hz,
                                  uint8_t counter_width_bits,
                                  uint32_t requested_frequency_hz,
                                  bsp_pwm_solution_t *solution)
{
    const uint64_t maximum_counts = counter_width_bits == 32U ?
                                    (uint64_t)UINT32_MAX + 1ULL :
                                    (uint64_t)UINT16_MAX + 1ULL;
    uint64_t best_error_hz = UINT64_MAX;
    uint64_t best_counts = 0U;
    uint32_t prescaler;
    bool has_solution = false;

    if((solution == NULL) || (timer_clock_hz == 0U) ||
       (requested_frequency_hz == 0U) ||
       ((counter_width_bits != 16U) && (counter_width_bits != 32U)))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    for(prescaler = 0U; prescaler <= UINT16_MAX; prescaler++)
    {
        const uint64_t divider = (uint64_t)requested_frequency_hz *
                                 (prescaler + 1ULL);
        const uint64_t counts = ((uint64_t)timer_clock_hz + divider / 2ULL) /
                                divider;
        uint64_t achieved_hz;
        uint64_t error_hz;

        if((counts == 0U) || (counts > maximum_counts))
        {
            continue;
        }

        achieved_hz = (uint64_t)timer_clock_hz /
                      ((prescaler + 1ULL) * counts);
        error_hz = achieved_hz > requested_frequency_hz ?
                   achieved_hz - requested_frequency_hz :
                   requested_frequency_hz - achieved_hz;

        if((error_hz < best_error_hz) ||
           ((error_hz == best_error_hz) && (counts > best_counts)))
        {
            best_error_hz = error_hz;
            best_counts = counts;
            solution->prescaler = prescaler;
            solution->auto_reload = (uint32_t)(counts - 1ULL);
            solution->achieved_frequency_hz = (uint32_t)achieved_hz;
            has_solution = true;

            if((error_hz == 0U) && (prescaler == 0U))
            {
                break;
            }
        }
    }

    if(!has_solution)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    solution->error_ppm = (uint32_t)((best_error_hz * 1000000ULL) /
                                     requested_frequency_hz);
    return BSP_STATUS_OK;
}

/**
 * @brief Apply a solved PWM configuration to the owned timer channel.
 */
static bsp_status_t bsp_pwm_apply(bsp_pwm_stm32h5_context_t *context,
                                  const bsp_pwm_config_t *config,
                                  bsp_pwm_result_t *result)
{
    bsp_pwm_solution_t solution;
    TIM_OC_InitTypeDef output = {0};
    bsp_status_t status;
    uint32_t timer_clock_hz;
    uint64_t period_counts;

    if((context == NULL) || (context->descriptor == NULL) || (config == NULL) ||
       (config->duty_permille > 1000U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    status = bsp_clock_get_hz(context->descriptor->clock_id, &timer_clock_hz);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = bsp_pwm_solve(timer_clock_hz,
                           context->descriptor->counter_width_bits,
                           config->frequency_hz,
                           &solution);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    context->handle.Instance = context->descriptor->instance;
    context->handle.Init.Prescaler = solution.prescaler;
    context->handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    context->handle.Init.Period = solution.auto_reload;
    context->handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    context->handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if(HAL_TIM_PWM_Init(&context->handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    period_counts = (uint64_t)solution.auto_reload + 1ULL;
    output.Pulse = (uint32_t)((period_counts * config->duty_permille + 500ULL) /
                              1000ULL);
    output.OCMode = config->duty_permille == 0U ? TIM_OCMODE_FORCED_INACTIVE :
                    config->duty_permille == 1000U ? TIM_OCMODE_FORCED_ACTIVE :
                    TIM_OCMODE_PWM1;
    output.OCPolarity = context->descriptor->active_low ?
                        TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;

    if(HAL_TIM_PWM_ConfigChannel(&context->handle,
                                 &output,
                                 context->descriptor->channel) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    context->result.requested_frequency_hz = config->frequency_hz;
    context->result.achieved_frequency_hz = solution.achieved_frequency_hz;
    context->result.error_ppm = solution.error_ppm;
    context->result.timer_clock_hz = timer_clock_hz;
    context->result.prescaler = solution.prescaler;
    context->result.auto_reload = solution.auto_reload;
    context->result.compare = output.Pulse;
    context->result.requested_duty_permille = config->duty_permille;

    if(result != NULL)
    {
        *result = context->result;
    }

    return BSP_STATUS_OK;
}

/**
 * @brief Implement bsp_pwm_stm32h5_init() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_stm32h5_init(bsp_pwm_stm32h5_context_t *context,
                                  const bsp_pwm_stm32h5_descriptor_t *descriptor,
                                  const bsp_pwm_config_t *config,
                                  bsp_pwm_result_t *result)
{
    bsp_status_t status;

    if((context == NULL) || (descriptor == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(context->is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    context->descriptor = descriptor;
    status = bsp_pwm_apply(context, config, result);
    if(status == BSP_STATUS_OK)
    {
        context->is_initialized = true;
    }
    return status;
}

/**
 * @brief Implement bsp_pwm_stm32h5_configure() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_stm32h5_configure(bsp_pwm_stm32h5_context_t *context,
                                       const bsp_pwm_config_t *config,
                                       bsp_pwm_result_t *result)
{
    return (context == NULL) || !context->is_initialized ? BSP_STATUS_NOT_READY :
           bsp_pwm_apply(context, config, result);
}

/**
 * @brief Implement bsp_pwm_stm32h5_start() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_stm32h5_start(bsp_pwm_stm32h5_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_TIM_PWM_Start(&context->handle, context->descriptor->channel) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/**
 * @brief Implement bsp_pwm_stm32h5_stop() as documented by its interface contract.
 */
bsp_status_t bsp_pwm_stm32h5_stop(bsp_pwm_stm32h5_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_TIM_PWM_Stop(&context->handle, context->descriptor->channel) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}
