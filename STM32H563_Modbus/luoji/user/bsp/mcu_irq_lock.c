/**
 * @file mcu_irq_lock.c
 * @brief STM32H5 interrupt lock and restore implementation.
 */

#include "bsp_irq_lock.h"

#include "stm32h5xx.h"

/**
 * @brief Implement bsp_irq_lock() as documented by its interface contract.
 */
bsp_irq_state_t bsp_irq_lock(void)
{
    const bsp_irq_state_t state = __get_PRIMASK();

    __disable_irq();
    __DMB();
    return state;
}

/**
 * @brief Implement bsp_irq_unlock() as documented by its interface contract.
 */
void bsp_irq_unlock(bsp_irq_state_t state)
{
    __DMB();
    __set_PRIMASK(state);
}
