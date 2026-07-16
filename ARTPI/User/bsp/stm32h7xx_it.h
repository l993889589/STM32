/**
 * @file stm32h7xx_it.h
 * @brief ART-Pi H750 interrupt-handler declarations.
 */

#ifndef STM32H7XX_IT_H
#define STM32H7XX_IT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void EXTI3_IRQHandler(void);

#endif
