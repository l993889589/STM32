/**
 * @file bsp_stop.c
 * @brief Minimal board-safe fatal-stop implementation.
 */

#include "bsp_stop.h"

#include "stm32f4xx.h"

/** @brief Mask interrupts and wait for an external reset. */
void bsp_stop_on_error(void)
{
    __disable_irq();
    for(;;)
    {
        __WFI();
    }
}
