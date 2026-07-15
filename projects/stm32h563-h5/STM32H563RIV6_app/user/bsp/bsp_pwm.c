/**
 * @file bsp_pwm.c
 * @brief PWM initialization, STM32 hardware control, interrupts, and public BSP API.
 */


#include <stdbool.h>
#include <stdint.h>

#include "bsp_clock.h"
#include "bsp_pwm.h"
#include "stm32h5xx_hal.h"

/** @brief Immutable MCU timer binding owned by one board PWM role. */
typedef struct
{
    TIM_TypeDef *instance;
    uint32_t channel;
    bsp_clock_id_t clock_id;
    uint8_t counter_width_bits;
    bool active_low;
} bsp_pwm_hw_descriptor_t;

/** @brief Mutable timer handle and achieved PWM state. */
typedef struct
{
    TIM_HandleTypeDef handle;
    const bsp_pwm_hw_descriptor_t *descriptor;
    bsp_pwm_result_t result;
    bool is_initialized;
} bsp_pwm_hw_context_t;

/**
 * @brief Initialize one timer channel using a physical-unit solver.
 * @param context Static context owned by the board role.
 * @param descriptor Timer instance, channel, width, clock, and polarity.
 * @param config Requested frequency and duty.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_hw_init(bsp_pwm_hw_context_t *context,
                          const bsp_pwm_hw_descriptor_t *descriptor,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result);

/**
 * @brief Solve and apply a new PWM frequency and duty.
 * @param context Initialized context.
 * @param config Requested physical values.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t bsp_pwm_hw_configure(bsp_pwm_hw_context_t *context,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result);

/** @brief Start an initialized timer channel. @param context PWM context. @return BSP status. */
bsp_status_t bsp_pwm_hw_start(bsp_pwm_hw_context_t *context);
/** @brief Stop an initialized timer channel. @param context PWM context. @return BSP status. */
bsp_status_t bsp_pwm_hw_stop(bsp_pwm_hw_context_t *context);


#include "bsp_pwm.h"

#include <stddef.h>

#include "bsp_config.h"

static const bsp_pwm_hw_descriptor_t g_lcd_backlight_descriptor =
{
    .instance = BOARD_PWM_LCD_INSTANCE,
    .channel = BOARD_PWM_LCD_CHANNEL,
    .clock_id = BSP_CLOCK_TIMER_APB1,
    .counter_width_bits = 32U,
    .active_low = BOARD_PWM_LCD_ACTIVE_LOW != 0U
};

static bsp_pwm_hw_context_t g_lcd_backlight_context;

/** @brief Resolve one logical PWM role to its static STM32 context. */
static bsp_pwm_hw_context_t *bsp_pwm_get_context(bsp_pwm_role_t role)
{
    return role == BOARD_PWM_LCD_BACKLIGHT ? &g_lcd_backlight_context : NULL;
}

/** @brief Implement bsp_pwm_init() for the PB11 TIM2_CH4 binding. */
bsp_status_t bsp_pwm_init(bsp_pwm_role_t role,
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

    return bsp_pwm_hw_init(&g_lcd_backlight_context,
                        &g_lcd_backlight_descriptor,
                        config,
                        result);
}

/** @brief Implement bsp_pwm_configure() through the role-owned context. */
bsp_status_t bsp_pwm_configure(bsp_pwm_role_t role,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result)
{
    bsp_pwm_hw_context_t *context = bsp_pwm_get_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT :
           bsp_pwm_hw_configure(context, config, result);
}

/** @brief Implement bsp_pwm_start() through the role-owned context. */
bsp_status_t bsp_pwm_start(bsp_pwm_role_t role)
{
    bsp_pwm_hw_context_t *context = bsp_pwm_get_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT : bsp_pwm_hw_start(context);
}

/** @brief Implement bsp_pwm_stop() through the role-owned context. */
bsp_status_t bsp_pwm_stop(bsp_pwm_role_t role)
{
    bsp_pwm_hw_context_t *context = bsp_pwm_get_context(role);
    return context == NULL ? BSP_STATUS_INVALID_ARGUMENT : bsp_pwm_hw_stop(context);
}

/** @brief Implement bsp_pwm_get_result() through the role-owned context. */
bsp_status_t bsp_pwm_get_result(bsp_pwm_role_t role, bsp_pwm_result_t *result)
{
    bsp_pwm_hw_context_t *context = bsp_pwm_get_context(role);

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

/* STM32 hardware implementation. */

#include <limits.h>
#include <stddef.h>

/** @brief Internal prescaler and period solution. */
typedef struct
{
    uint32_t prescaler;
    uint32_t auto_reload;
    uint32_t achieved_frequency_hz;
    uint32_t error_ppm;
} bsp_pwm_hw_solution_t;

/** @brief Solve prescaler and period while maximizing useful resolution. */
static bsp_status_t bsp_pwm_hw_solve(uint32_t timer_clock_hz,
                                  uint8_t counter_width_bits,
                                  uint32_t requested_frequency_hz,
                                  bsp_pwm_hw_solution_t *solution)
{
    uint64_t maximum_counts;
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

    maximum_counts = counter_width_bits == 32U ?
                     (uint64_t)UINT32_MAX + 1ULL :
                     (uint64_t)UINT16_MAX + 1ULL;

    for(prescaler = 0U; prescaler <= UINT16_MAX; prescaler++)
    {
        uint64_t divider = (uint64_t)requested_frequency_hz *
                           (prescaler + 1ULL);
        uint64_t counts = ((uint64_t)timer_clock_hz + divider / 2ULL) /
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

/** @brief Apply a solved configuration to one owned timer channel. */
static bsp_status_t bsp_pwm_hw_apply(bsp_pwm_hw_context_t *context,
                                  const bsp_pwm_config_t *config,
                                  bsp_pwm_result_t *result)
{
    bsp_pwm_hw_solution_t solution;
    TIM_OC_InitTypeDef output = {0};
    bsp_status_t status;
    uint32_t timer_clock_hz;
    uint64_t period_counts;
    uint64_t compare;

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

    status = bsp_pwm_hw_solve(timer_clock_hz,
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
    compare = (period_counts * config->duty_permille + 500ULL) / 1000ULL;
    output.Pulse = compare > UINT32_MAX ? UINT32_MAX : (uint32_t)compare;
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

/** @brief Implement bsp_pwm_hw_init() for one statically owned context. */
bsp_status_t bsp_pwm_hw_init(bsp_pwm_hw_context_t *context,
                          const bsp_pwm_hw_descriptor_t *descriptor,
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
    status = bsp_pwm_hw_apply(context, config, result);
    if(status == BSP_STATUS_OK)
    {
        context->is_initialized = true;
    }
    return status;
}

/** @brief Implement bsp_pwm_hw_configure() for an initialized context. */
bsp_status_t bsp_pwm_hw_configure(bsp_pwm_hw_context_t *context,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result)
{
    return (context == NULL) || !context->is_initialized ?
           BSP_STATUS_NOT_READY : bsp_pwm_hw_apply(context, config, result);
}

/** @brief Implement bsp_pwm_hw_start() for one timer channel. */
bsp_status_t bsp_pwm_hw_start(bsp_pwm_hw_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_TIM_PWM_Start(&context->handle, context->descriptor->channel) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Implement bsp_pwm_hw_stop() for one timer channel. */
bsp_status_t bsp_pwm_hw_stop(bsp_pwm_hw_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_TIM_PWM_Stop(&context->handle, context->descriptor->channel) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}
