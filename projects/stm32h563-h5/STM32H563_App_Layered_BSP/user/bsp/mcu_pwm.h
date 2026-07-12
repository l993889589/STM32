/**
 * @file mcu_pwm.h
 * @brief Private STM32H5 timer/PWM solver interface.
 */

#ifndef MCU_PWM_H
#define MCU_PWM_H

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
} mcu_pwm_descriptor_t;

/** @brief Mutable timer handle and achieved PWM state. */
typedef struct
{
    TIM_HandleTypeDef handle;
    const mcu_pwm_descriptor_t *descriptor;
    bsp_pwm_result_t result;
    bool is_initialized;
} mcu_pwm_context_t;

/**
 * @brief Initialize one timer channel using a physical-unit solver.
 * @param context Static context owned by the board role.
 * @param descriptor Timer instance, channel, width, clock, and polarity.
 * @param config Requested frequency and duty.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t mcu_pwm_init(mcu_pwm_context_t *context,
                          const mcu_pwm_descriptor_t *descriptor,
                          const bsp_pwm_config_t *config,
                          bsp_pwm_result_t *result);

/**
 * @brief Solve and apply a new PWM frequency and duty.
 * @param context Initialized context.
 * @param config Requested physical values.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t mcu_pwm_configure(mcu_pwm_context_t *context,
                               const bsp_pwm_config_t *config,
                               bsp_pwm_result_t *result);

/** @brief Start an initialized timer channel. @param context PWM context. @return BSP status. */
bsp_status_t mcu_pwm_start(mcu_pwm_context_t *context);
/** @brief Stop an initialized timer channel. @param context PWM context. @return BSP status. */
bsp_status_t mcu_pwm_stop(mcu_pwm_context_t *context);

#endif /* MCU_PWM_H */
