/**
 * @file bsp_timer.c
 * @brief RTOS-neutral millisecond time and software timer helpers.
 *
 * HAL_GetTick is supplied by the target timebase. This ThreadX target uses
 * TIM17 so the RTOS remains the sole owner of SysTick.
 */

#include "bsp_timer.h"

#include "stm32h5xx_hal.h"

/** @brief Initialize the stateless software timer service. */
void bsp_timer_init(void)
{
}

/** @brief Return the wrapping monotonic millisecond counter. */
uint32_t bsp_timer_get_ms(void)
{
    return HAL_GetTick();
}

/** @brief Perform a blocking millisecond delay in non-ISR context. */
void bsp_timer_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/** @brief Arm a software timer from the current millisecond count. */
void bsp_timer_start(bsp_timer_t *timer, uint32_t period_ms)
{
    if(!timer)
        return;

    timer->start_ms = bsp_timer_get_ms();
    timer->period_ms = period_ms;
    timer->active = true;
}

/** @brief Disarm one software timer. */
void bsp_timer_stop(bsp_timer_t *timer)
{
    if(!timer)
        return;

    timer->active = false;
}

/** @brief Calculate a wrap-safe millisecond delta. */
uint32_t bsp_timer_elapsed_ms(uint32_t start_ms)
{
    return bsp_timer_get_ms() - start_ms;
}

/** @brief Report whether an armed software timer has reached its deadline. */
bool bsp_timer_expired(const bsp_timer_t *timer)
{
    if(!timer || !timer->active)
        return false;

    return bsp_timer_elapsed_ms(timer->start_ms) >= timer->period_ms;
}

/** @brief Consume one periodic deadline while limiting accumulated drift. */
bool bsp_timer_poll(bsp_timer_t *timer)
{
    if(!bsp_timer_expired(timer))
        return false;

    timer->start_ms += timer->period_ms;
    if(bsp_timer_elapsed_ms(timer->start_ms) >= timer->period_ms)
        timer->start_ms = bsp_timer_get_ms();

    return true;
}
