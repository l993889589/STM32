/**
 * @file bsp_control.h
 * @brief Safe ownership of the provisional PB14/PB15 control outputs.
 */

#ifndef BSP_CONTROL_H
#define BSP_CONTROL_H

#include "bsp_status.h"

/** @brief Configure both provisional outputs low without assigning policy. */
bsp_status_t bsp_control_init(void);

#endif /* BSP_CONTROL_H */
