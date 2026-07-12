/**
 * @file bsp_board.h
 * @brief Board-level initialization interface for populated external devices.
 */

#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bind and initialize board-owned external-device resources.
 * @return Zero on success, otherwise -1.
 */
int bsp_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
