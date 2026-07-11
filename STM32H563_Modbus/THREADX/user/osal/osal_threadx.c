/**
 * @file osal_threadx.c
 * @brief ThreadX implementation of the runtime-neutral OS abstraction.
 */

#include "osal.h"

#include "tx_api.h"

/** @brief Return the ThreadX monotonic time converted to milliseconds. */
uint32_t osal_time_get_ms(void)
{
    return (uint32_t)(((uint64_t)tx_time_get() * 1000U) /
                      TX_TIMER_TICKS_PER_SECOND);
}

/** @brief Suspend the current ThreadX task for at least the requested duration. */
void osal_sleep_ms(uint32_t delay_ms)
{
    ULONG ticks = (ULONG)(((uint64_t)delay_ms * TX_TIMER_TICKS_PER_SECOND +
                           999U) / 1000U);

    if((delay_ms != 0U) && (ticks == 0U))
    {
        ticks = 1U;
    }
    if(ticks != 0U)
    {
        (void)tx_thread_sleep(ticks);
    }
}

/** @brief Relinquish the remainder of the current ThreadX time slice. */
void osal_yield(void)
{
    tx_thread_relinquish();
}
