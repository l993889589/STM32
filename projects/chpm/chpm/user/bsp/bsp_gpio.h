/**
 * @file bsp_gpio.h
 * @brief Public CHPM GPIO initialization interface.
 */

#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include "bsp_status.h"

/** @brief Configure all CHPM board GPIO outputs to safe initial levels. */
bsp_status_t bsp_gpio_init(void);

#endif
