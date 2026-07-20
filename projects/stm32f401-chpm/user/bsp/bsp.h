/**
 * @file bsp.h
 * @brief Ordered composition of the CHPM flat board support package.
 */

#ifndef BSP_H
#define BSP_H

#include "bsp_status.h"

typedef enum
{
    BSP_INIT_STAGE_NONE = 0,
    BSP_INIT_STAGE_CLOCK,
    BSP_INIT_STAGE_DWT,
    BSP_INIT_STAGE_SAFE_GPIO,
    BSP_INIT_STAGE_BUSES,
    BSP_INIT_STAGE_UART,
    BSP_INIT_STAGE_READY
} bsp_init_stage_t;

/** @brief Initialize HAL and configure the fixed board clock tree. */
bsp_status_t bsp_startup(void);

/** @brief Initialize every required board owner in safe dependency order. */
bsp_status_t bsp_init(void);

/** @brief Start receive delivery after protocol owners bind their callbacks. */
bsp_status_t bsp_start_io(void);

/** @brief Return the most recently reached initialization stage. */
bsp_init_stage_t bsp_init_stage(void);

#endif /* BSP_H */
