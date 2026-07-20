/**
 * @file stm32f4xx_it.c
 * @brief Cortex-M4 exception handlers; peripheral vectors live with BSP owners.
 */

#include "stm32f4xx_it.h"

/** @brief Hold after an unexpected non-maskable interrupt. */
void NMI_Handler(void)
{
    for(;;)
    {
    }
}

/** @brief Hold after a hard fault for debugger inspection or watchdog reset. */
void HardFault_Handler(void)
{
    for(;;)
    {
    }
}

/** @brief Hold after a memory-management fault. */
void MemManage_Handler(void)
{
    for(;;)
    {
    }
}

/** @brief Hold after a bus fault. */
void BusFault_Handler(void)
{
    for(;;)
    {
    }
}

/** @brief Hold after an undefined instruction or illegal processor state. */
void UsageFault_Handler(void)
{
    for(;;)
    {
    }
}

/** @brief Reserved debug-monitor hook. */
void DebugMon_Handler(void)
{
}
