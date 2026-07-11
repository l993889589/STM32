/**
 * @file bsp_fault_stm32h5.c
 * @brief STM32H5 terminal fatal handling.
 */

#include "bsp_fatal.h"

#include "stm32h5xx.h"

/**
 * @brief Implement bsp_fatal_stop() as documented by its interface contract.
 */
void bsp_fatal_stop(bsp_fatal_stage_t stage, bsp_status_t status)
{
    bsp_diagnostics_record_fatal(stage, status);
    __disable_irq();

    for(;;)
    {
        __DSB();
        __WFI();
    }
}
