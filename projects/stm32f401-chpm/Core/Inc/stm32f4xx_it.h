/**
 * @file stm32f4xx_it.h
 * @brief Cortex-M4 exception declarations for the CHPM target.
 */

#ifndef STM32F4XX_IT_H
#define STM32F4XX_IT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

#endif /* STM32F4XX_IT_H */
