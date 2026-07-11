/**
 * @file bsp_time.h
 * @brief Monotonic millisecond time and deadline interface.
 */

#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t expires_at_ms;
} bsp_deadline_t;

/**
 * Read the monotonic HAL timebase.
 * @return Milliseconds modulo 2^32.
 */
uint32_t bsp_time_get_ms(void);
/**
 * Calculate wrap-safe elapsed milliseconds.
 * @param started_at_ms Earlier timestamp from bsp_time_get_ms().
 * @return Elapsed milliseconds modulo 2^32.
 */
uint32_t bsp_time_elapsed_ms(uint32_t started_at_ms);
/**
 * Busy-wait for a bounded number of milliseconds in bare-metal context.
 * @param delay_ms Delay duration in milliseconds.
 * @note Do not use for long waits or from interrupt context.
 */
void bsp_time_delay_ms(uint32_t delay_ms);
/**
 * Start or restart a wrap-safe deadline.
 * @param deadline Caller-owned deadline object.
 * @param timeout_ms Relative timeout in milliseconds.
 */
void bsp_deadline_start(bsp_deadline_t *deadline, uint32_t timeout_ms);
/**
 * Test a deadline using wrap-safe signed subtraction.
 * @param deadline Deadline object to test.
 * @return True when expired or when deadline is NULL.
 */
bool bsp_deadline_has_expired(const bsp_deadline_t *deadline);

#ifdef __cplusplus
}
#endif

#endif
