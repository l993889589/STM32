/**
 * @file osal.h
 * @brief Runtime-neutral time, sleep, and yield abstraction.
 */

#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>

/**
 * Read runtime-neutral monotonic time.
 * @return Milliseconds modulo 2^32.
 */
uint32_t osal_time_get_ms(void);
/**
 * Sleep or delay the current execution context.
 * @param delay_ms Requested delay in milliseconds.
 * @note Bare-metal implementation busy-waits; ThreadX implementation blocks the task.
 */
void osal_sleep_ms(uint32_t delay_ms);
/**
 * Yield execution according to the selected runtime backend.
 */
void osal_yield(void);

#endif
