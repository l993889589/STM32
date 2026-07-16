/**
 * @file board.c
 * @brief Deterministic board-level BSP initialization and I/O start sequence.
 */

#include "board.h"

#include "bsp_gpio.h"
#include "bsp_timer.h"
#include "bsp_uart.h"

static board_init_stage_t current_stage;

/** @brief Initialize GPIO, UART and timer modules in dependency order. */
bsp_status_t board_init(void)
{
    bsp_status_t status;

    if(current_stage == BOARD_INIT_STAGE_READY)
        return BSP_STATUS_ALREADY_INITIALIZED;

    current_stage = BOARD_INIT_STAGE_GPIO;
    status = bsp_gpio_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BOARD_INIT_STAGE_UART;
    status = bsp_uart_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BOARD_INIT_STAGE_TIMER;
    status = bsp_timer_init();
    if(status != BSP_STATUS_OK && status != BSP_STATUS_ALREADY_INITIALIZED)
        return status;

    current_stage = BOARD_INIT_STAGE_READY;
    return BSP_STATUS_OK;
}

/** @brief Start board receive paths after all BSP modules are ready. */
bsp_status_t board_start_io(void)
{
    if(current_stage != BOARD_INIT_STAGE_READY)
        return BSP_STATUS_BUSY;
    return bsp_uart_start_rx();
}

/** @brief Return the most recently reached board initialization stage. */
board_init_stage_t board_init_stage(void)
{
    return current_stage;
}
