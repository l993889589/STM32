/**
 * @file bsp_timebase.h
 * @brief Private TIM17 HAL timebase interrupt-dispatch interface.
 */

#ifndef MCU_HAL_TIMEBASE_H
#define MCU_HAL_TIMEBASE_H

/** @brief Dispatch the TIM17 HAL millisecond tick from ISR context. */
void bsp_timebase_irq_from_isr(void);

#endif /* MCU_HAL_TIMEBASE_H */
