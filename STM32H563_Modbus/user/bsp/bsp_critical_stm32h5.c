/**
 * @file bsp_critical_stm32h5.c
 * @brief STM32H5 critical-section implementation.
 */

#include "bsp_critical.h"

#include "stm32h5xx.h"

/**
 * @brief Implement bsp_critical_enter() as documented by its interface contract.
 */
bsp_critical_state_t bsp_critical_enter(void)
{
    const bsp_critical_state_t state = __get_PRIMASK();

    __disable_irq();
    __DMB();
    return state;
}

/**
 * @brief Implement bsp_critical_exit() as documented by its interface contract.
 */
void bsp_critical_exit(bsp_critical_state_t state)
{
    __DMB();
    __set_PRIMASK(state);
}
