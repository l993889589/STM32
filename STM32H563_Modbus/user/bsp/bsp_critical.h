/**
 * @file bsp_critical.h
 * @brief Interrupt-mask preserving critical-section interface.
 */

#ifndef BSP_CRITICAL_H
#define BSP_CRITICAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t bsp_critical_state_t;

/**
 * Mask interrupts while preserving the caller's previous PRIMASK state.
 * @return State token that must be passed to bsp_critical_exit().
 */
bsp_critical_state_t bsp_critical_enter(void);
/**
 * Restore the interrupt-mask state saved on critical-section entry.
 * @param state Token returned by bsp_critical_enter().
 */
void bsp_critical_exit(bsp_critical_state_t state);

#ifdef __cplusplus
}
#endif

#endif
