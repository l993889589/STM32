/**
 * @file stm32h5xx_it.c
 * @brief Fault and HAL-timebase interrupts for the ThreadX Modbus target.
 */

#include "stm32h5xx_hal.h"

extern TIM_HandleTypeDef htim17;

/** @brief Stop on a non-maskable interrupt. */
void NMI_Handler(void)
{
    while(1)
    {
    }
}

/** @brief Stop on a memory-management fault. */
void MemManage_Handler(void)
{
    while(1)
    {
    }
}

/** @brief Stop on a bus fault. */
void BusFault_Handler(void)
{
    while(1)
    {
    }
}

/** @brief Keep the optional debug monitor vector available. */
void DebugMon_Handler(void)
{
}

/** @brief Dispatch the independent HAL millisecond timebase. */
void TIM17_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim17);
}
