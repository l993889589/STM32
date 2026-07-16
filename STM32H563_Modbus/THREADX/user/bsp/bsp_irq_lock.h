/**
 * @file bsp_irq_lock.h
 * @brief Interrupt-mask preserving critical-section interface.
 */

#ifndef BSP_IRQ_LOCK_H
#define BSP_IRQ_LOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t bsp_irq_state_t;

/**
 * Mask interrupts while preserving the caller's previous PRIMASK state.
 * @return State token that must be passed to bsp_irq_unlock().
 */
bsp_irq_state_t bsp_irq_lock(void);
/**
 * Restore the interrupt-mask state saved on critical-section entry.
 * @param state Token returned by bsp_irq_lock().
 */
void bsp_irq_unlock(bsp_irq_state_t state);

#ifdef __cplusplus
}
#endif

#endif
