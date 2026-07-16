/**
 * @file bsp_timer.h
 * @brief Millisecond clock and lightweight software timer API.
 */

#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t start_ms;
    uint32_t period_ms;
    bool active;
} bsp_timer_t;

/** @brief Initialize the stateless software timer service. */
void bsp_timer_init(void);
/** @brief Return the wrapping monotonic millisecond counter. */
uint32_t bsp_timer_get_ms(void);
/** @brief Perform a blocking millisecond delay outside interrupt context. */
void bsp_timer_delay_ms(uint32_t ms);
/** @brief Arm a software timer with a millisecond period. */
void bsp_timer_start(bsp_timer_t *timer, uint32_t period_ms);
/** @brief Disarm one software timer. */
void bsp_timer_stop(bsp_timer_t *timer);
/** @brief Report whether an armed software timer has expired. */
bool bsp_timer_expired(const bsp_timer_t *timer);
/** @brief Consume one periodic deadline and re-arm the timer. */
bool bsp_timer_poll(bsp_timer_t *timer);
/** @brief Return a wrap-safe delta from a previous millisecond sample. */
uint32_t bsp_timer_elapsed_ms(uint32_t start_ms);

#endif /* BSP_TIMER_H */
