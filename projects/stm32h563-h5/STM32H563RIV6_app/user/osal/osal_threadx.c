/**
 * @file osal_threadx.c
 * @brief ThreadX backend for the RTOS-neutral mutex interface.
 *
 * Only this target-specific adapter includes tx_api.h. Reusable BSP and
 * device drivers depend on osal_mutex.h and can be copied to a bare-metal
 * project with a different backend implementation.
 */

#include "osal_mutex.h"

#include <limits.h>

#include "bsp_irq_lock.h"
#include "tx_api.h"

typedef char osal_mutex_storage_must_fit[
    (sizeof(TX_MUTEX) <= sizeof(((osal_mutex_t *)0)->native_storage)) ? 1 : -1];

/** @brief Convert a millisecond timeout to a rounded-up ThreadX tick count. */
static ULONG osal_timeout_to_ticks(uint32_t timeout_ms)
{
    uint64_t ticks;

    if(timeout_ms == 0U)
        return TX_NO_WAIT;

    ticks = ((uint64_t)timeout_ms * TX_TIMER_TICKS_PER_SECOND + 999ULL) / 1000ULL;
    if(ticks > (uint64_t)ULONG_MAX)
        return (ULONG)ULONG_MAX;

    return (ULONG)ticks;
}

/** @brief Return the native ThreadX object stored inside an opaque mutex. */
static TX_MUTEX *osal_native_mutex(osal_mutex_t *mutex)
{
    return (TX_MUTEX *)(void *)mutex->native_storage;
}

/** @brief Create one ThreadX mutex without exposing ThreadX to its caller. */
bool osal_mutex_init(osal_mutex_t *mutex, const char *name)
{
    if(mutex == NULL)
        return false;

    if(mutex->initialized != 0U)
        return true;

    mutex->early_lock = 0U;
    if(tx_mutex_create(osal_native_mutex(mutex), (CHAR *)name, TX_INHERIT) != TX_SUCCESS)
        return false;

    mutex->initialized = 1U;
    return true;
}

/** @brief Acquire through ThreadX, with a safe pre-kernel single-owner path. */
bool osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms)
{
    bsp_irq_state_t irq_state;

    if((mutex == NULL) || (mutex->initialized == 0U))
        return false;

    if(tx_thread_identify() != TX_NULL)
    {
        return tx_mutex_get(osal_native_mutex(mutex),
                            osal_timeout_to_ticks(timeout_ms)) == TX_SUCCESS;
    }

    irq_state = bsp_irq_lock();
    if(mutex->early_lock != 0U)
    {
        bsp_irq_unlock(irq_state);
        return false;
    }
    mutex->early_lock = 1U;
    bsp_irq_unlock(irq_state);
    return true;
}

/** @brief Release through the same kernel or pre-kernel path used to lock. */
void osal_mutex_unlock(osal_mutex_t *mutex)
{
    bsp_irq_state_t irq_state;

    if((mutex == NULL) || (mutex->initialized == 0U))
        return;

    if(tx_thread_identify() != TX_NULL)
    {
        (void)tx_mutex_put(osal_native_mutex(mutex));
        return;
    }

    irq_state = bsp_irq_lock();
    mutex->early_lock = 0U;
    bsp_irq_unlock(irq_state);
}
