/**
 * @file board.h
 * @brief Board-level BSP initialization state and public entry points.
 */

#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    BOARD_INIT_STAGE_NONE = 0,
    BOARD_INIT_STAGE_GPIO,
    BOARD_INIT_STAGE_UART,
    BOARD_INIT_STAGE_TIMER,
    BOARD_INIT_STAGE_READY
} board_init_stage_t;

/** @brief Initialize all board BSP modules in dependency order. */
bsp_status_t board_init(void);
/** @brief Start board receive paths after successful initialization. */
bsp_status_t board_start_io(void);
/** @brief Return the current board initialization stage. */
board_init_stage_t board_init_stage(void);

#endif
