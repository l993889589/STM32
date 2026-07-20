/**
 * @file bsp_reset.h
 * @brief Reset-cause capture and system-reset interface.
 */

#ifndef BSP_RESET_H
#define BSP_RESET_H

#include <stdint.h>

#include "bsp_status.h"

#define BSP_RESET_CAUSE_PIN       (1UL << 0)
#define BSP_RESET_CAUSE_BROWNOUT  (1UL << 1)
#define BSP_RESET_CAUSE_SOFTWARE  (1UL << 2)
#define BSP_RESET_CAUSE_IWDG      (1UL << 3)
#define BSP_RESET_CAUSE_WWDG      (1UL << 4)
#define BSP_RESET_CAUSE_LOW_POWER (1UL << 5)

/** @brief Read latched reset causes without clearing them. */
bsp_status_t bsp_reset_get_causes(uint32_t *causes);

/** @brief Clear hardware reset flags after diagnostics capture. */
void bsp_reset_clear_causes(void);

/** @brief Request an immediate Cortex-M system reset. */
void bsp_system_reset(void);

#endif /* BSP_RESET_H */
