/**
 * @file bsp_irq_lock.h
 * @brief Save-and-restore interrupt lock interface.
 */

#ifndef BSP_IRQ_LOCK_H
#define BSP_IRQ_LOCK_H

#include <stdint.h>

/** @brief Saved interrupt-mask state. */
typedef uint32_t bsp_irq_state_t;

/** @brief Save PRIMASK and disable maskable interrupts. @return Previous PRIMASK. */
bsp_irq_state_t bsp_irq_lock(void);
/** @brief Restore a PRIMASK value returned by bsp_irq_lock(). */
void bsp_irq_unlock(bsp_irq_state_t state);

#endif /* BSP_IRQ_LOCK_H */
