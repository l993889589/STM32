/**
 * @file bsp_stop.h
 * @brief Unrecoverable-error safe-stop boundary for the STM32H563 board.
 */

#ifndef BSP_STOP_H
#define BSP_STOP_H

#include <stdint.h>

/** @brief Startup stage that entered the unrecoverable safe-stop state. */
typedef enum
{
    BSP_STOP_STAGE_NONE = 0,
    BSP_STOP_STAGE_CLOCK,
    BSP_STOP_STAGE_BSP_INIT,
    BSP_STOP_STAGE_RUNTIME,
    BSP_STOP_STAGE_NMI,
    BSP_STOP_STAGE_HARDFAULT,
    BSP_STOP_STAGE_MEMFAULT,
    BSP_STOP_STAGE_BUSFAULT,
    BSP_STOP_STAGE_USAGEFAULT
} bsp_stop_stage_t;

/**
 * @brief Disable interrupts, record the failed stage, and blink the status LED.
 * @param stage Startup or runtime stage that could not recover.
 * @note This function never returns and is safe before normal BSP GPIO init.
 */
void bsp_stop_on_error(bsp_stop_stage_t stage);

/**
 * @brief Return the most recent unrecoverable stage recorded in RAM.
 * @return Last stage passed to bsp_stop_on_error(), or BSP_STOP_STAGE_NONE.
 */
bsp_stop_stage_t bsp_stop_get_stage(void);

#endif /* BSP_STOP_H */
