/**
 * @file mcu_cpu_fault.c
 * @brief STM32H5 safe-stop and processor-fault handling.
 */

#include "bsp_stop.h"

#include "stm32h5xx.h"

/**
 * @brief Implement bsp_stop_on_error() as documented by its interface contract.
 */
void bsp_stop_on_error(bsp_error_stage_t stage, bsp_status_t status)
{
    bsp_health_record_error(stage, status);
    __disable_irq();

    for(;;)
    {
        __DSB();
        __WFI();
    }
}
