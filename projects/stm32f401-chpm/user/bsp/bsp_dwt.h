/**
 * @file bsp_dwt.h
 * @brief Cortex-M4 cycle counter and bounded busy-wait interface.
 */

#ifndef BSP_DWT_H
#define BSP_DWT_H

#include <stdint.h>

#include "bsp_status.h"

/** @brief Enable and clear the DWT cycle counter. */
bsp_status_t bsp_dwt_init(void);

/** @brief Return the current wrapping 32-bit core-cycle timestamp. */
uint32_t bsp_dwt_get_cycles(void);

/** @brief Return the current DWT counter frequency in hertz. */
uint32_t bsp_dwt_frequency_hz(void);

/** @brief Busy-wait for a number of core clock cycles. */
void bsp_dwt_delay_cycles(uint32_t cycles);

/** @brief Busy-wait for a number of microseconds. */
void bsp_dwt_delay_us(uint32_t delay_us);

/** @brief Busy-wait for a number of milliseconds. */
void bsp_dwt_delay_ms(uint32_t delay_ms);

#endif /* BSP_DWT_H */
