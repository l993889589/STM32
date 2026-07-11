/**
 * @file bsp.c
 * @brief Top-level BSP initialization entry point.
 */

#include "bsp.h"

#include "board_init.h"

/**
 * @brief Implement bsp_init() as documented by its interface contract.
 */
bsp_status_t bsp_init(void)
{
    return board_init();
}
