/**
 * @file bsp_microtime.h
 * @brief Backend-independent wrapping microsecond timestamp interface.
 *
 * Initialize once after the system clock is configured. Returned timestamps
 * wrap naturally at 32 bits; callers must calculate elapsed time by unsigned
 * subtraction.
 */

#ifndef BSP_MICROTIME_H
#define BSP_MICROTIME_H

#include <stdint.h>

#include "bsp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the microsecond timebase selected at compile time. */
bsp_status_t bsp_microtime_init(void);

/** @brief Return the current wrapping microsecond timestamp. */
uint32_t bsp_microtime_now_us(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_MICROTIME_H */
