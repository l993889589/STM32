/**
 * @file mcu_pwm.h
 * @brief Private STM32H5 PWM context and binding interface.
 */

#ifndef MCU_PWM_H
#define MCU_PWM_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_clock.h"
#include "bsp_pwm.h"
#include "stm32h5xx_hal.h"

typedef struct
{
    TIM_TypeDef *instance;
    uint32_t channel;
    bsp_clock_id_t clock_id;
    uint8_t counter_width_bits;
    bool active_low;
} mcu_pwm_descriptor_t;

typedef struct
{
    TIM_HandleTypeDef handle;
    const mcu_pwm_descriptor_t *descriptor;
    bsp_pwm_result_t result;
    bool is_initialized;
} mcu_pwm_context_t;

/**
 * Initialize one STM32H5 timer channel from a board descriptor.
 * @param context Static mutable PWM context owned by the board role.
 * @param descriptor STM32H5 timer, channel, GPIO, and clock binding.
 * @param config Requested physical-unit configuration.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t mcu_pwm_init(mcu_pwm_context_t *context,
                                  const mcu_pwm_descriptor_t *descriptor,
                                  const bsp_pwm_config_t *config,
                                  bsp_pwm_result_t *result);
/**
 * Solve and apply a new STM32H5 PWM frequency and duty.
 * @param context Initialized PWM context.
 * @param config Requested physical-unit configuration.
 * @param result Optional achieved configuration.
 * @return BSP status.
 */
bsp_status_t mcu_pwm_configure(mcu_pwm_context_t *context,
                                       const bsp_pwm_config_t *config,
                                       bsp_pwm_result_t *result);
/**
 * Start the bound STM32H5 PWM channel.
 * @param context Initialized PWM context.
 * @return BSP status.
 */
bsp_status_t mcu_pwm_start(mcu_pwm_context_t *context);
/**
 * Stop the bound STM32H5 PWM channel.
 * @param context Initialized PWM context.
 * @return BSP status.
 */
bsp_status_t mcu_pwm_stop(mcu_pwm_context_t *context);

#endif
