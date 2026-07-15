/**
 * @file bsp_dwt.h
 * @brief DWT cycle counter and short-delay API.
 */

#ifndef BSP_DWT_H
#define BSP_DWT_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Enable and reset the Cortex-M33 DWT cycle counter. */
bool bsp_dwt_init(void);
/** @brief Report whether the DWT cycle counter is running. */
bool bsp_dwt_is_enabled(void);
/** @brief Return the current wrapping DWT cycle count. */
uint32_t bsp_dwt_get_cycle(void);
/** @brief Return a wrapping microsecond timestamp. */
uint32_t bsp_dwt_get_us(void);
/** @brief Calculate a wrap-safe cycle delta. */
uint32_t bsp_dwt_elapsed_cycles(uint32_t start);
/** @brief Convert microseconds to core cycles with saturation. */
uint32_t bsp_dwt_us_to_cycles(uint32_t us);
/** @brief Busy-wait for a bounded number of core cycles. */
void bsp_dwt_delay_cycles(uint32_t cycles);
/** @brief Busy-wait for a short microsecond interval. */
void bsp_dwt_delay_us(uint32_t us);
/** @brief Busy-wait for a millisecond interval. */
void bsp_dwt_delay_ms(uint32_t ms);

#endif /* BSP_DWT_H */
