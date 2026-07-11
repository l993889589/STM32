/**
 * @file bsp.h
 * @brief Top-level BSP initialization interface.
 */

#ifndef BSP_H
#define BSP_H

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the selected board BSP in its required order.
 * @return BSP status from the board initialization sequence.
 */
bsp_status_t bsp_init(void);

#ifdef __cplusplus
}
#endif

#endif
