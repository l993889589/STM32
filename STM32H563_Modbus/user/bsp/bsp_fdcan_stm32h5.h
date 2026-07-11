/**
 * @file bsp_fdcan_stm32h5.h
 * @brief STM32H5 private FDCAN adapter contract.
 */

#ifndef BSP_FDCAN_STM32H5_H
#define BSP_FDCAN_STM32H5_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_fdcan.h"
#include "stm32h5xx_hal.h"

typedef struct
{
    FDCAN_HandleTypeDef handle;
    bsp_fdcan_diagnostics_t diagnostics;
    bool is_initialized;
    bool is_started;
    bool fd_enabled;
    bool bitrate_switch_enabled;
} bsp_fdcan_stm32h5_context_t;

/**
 * Initialize an STM32H5 FDCAN context after board clocks and pins are ready.
 * @param context Static driver context.
 * @param instance STM32 FDCAN peripheral instance.
 * @param kernel_clock_hz Selected FDCAN kernel-clock frequency.
 * @param config Requested public configuration.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_stm32h5_init(bsp_fdcan_stm32h5_context_t *context,
                                    FDCAN_GlobalTypeDef *instance,
                                    uint32_t kernel_clock_hz,
                                    const bsp_fdcan_config_t *config);
/**
 * Stop an STM32H5 FDCAN context.
 * @param context Static driver context.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_stm32h5_stop(bsp_fdcan_stm32h5_context_t *context);
/**
 * Stop and restart one initialized STM32H5 FDCAN context.
 * @param context Static driver context.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_stm32h5_recover(bsp_fdcan_stm32h5_context_t *context);
/**
 * Queue one public frame in the STM32H5 transmit FIFO.
 * @param context Static driver context.
 * @param frame Public frame description.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_stm32h5_send(bsp_fdcan_stm32h5_context_t *context,
                                    const bsp_fdcan_frame_t *frame);
/**
 * Try to receive one public frame from STM32H5 FIFO 0.
 * @param context Static driver context.
 * @param frame Receives a frame.
 * @param has_frame Receives true when a frame was copied.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_stm32h5_try_receive(bsp_fdcan_stm32h5_context_t *context,
                                           bsp_fdcan_frame_t *frame,
                                           bool *has_frame);
/**
 * Dispatch one STM32H5 FDCAN interrupt line.
 * @param context Static driver context.
 */
void bsp_fdcan_stm32h5_irq(bsp_fdcan_stm32h5_context_t *context);

#endif
