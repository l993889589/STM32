/**
 * @file mcu_irq_lock.c
 * @brief Cortex-M33 PRIMASK save-and-restore implementation.
 */

#include "bsp_irq_lock.h"

#include "stm32h5xx_hal.h"

/** @brief Save PRIMASK and disable maskable interrupts. */
bsp_irq_state_t bsp_irq_lock(void)
{
    bsp_irq_state_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

/** @brief Restore the exact interrupt-mask state saved by bsp_irq_lock(). */
void bsp_irq_unlock(bsp_irq_state_t state)
{
    __set_PRIMASK(state);
}
