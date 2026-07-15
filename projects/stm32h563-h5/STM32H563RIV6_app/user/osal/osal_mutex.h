/**
 * @file osal_mutex.h
 * @brief RTOS-neutral mutex interface used by reusable device drivers.
 *
 * The public object contains opaque, naturally aligned storage so BSP and
 * device code do not include an RTOS header. Each target supplies exactly one
 * backend, for example osal_threadx.c or a bare-metal critical-section port.
 */

#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include <stdbool.h>
#include <stdint.h>

#define OSAL_MUTEX_NATIVE_WORDS 16U

typedef struct
{
    uintptr_t native_storage[OSAL_MUTEX_NATIVE_WORDS];
    volatile uint32_t early_lock;
    uint32_t initialized;
} osal_mutex_t;

/** @brief Create one mutex using the backend selected by the current target. */
bool osal_mutex_init(osal_mutex_t *mutex, const char *name);

/** @brief Acquire a mutex, waiting for at most timeout_ms milliseconds. */
bool osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms);

/** @brief Release a mutex previously acquired by the current context. */
void osal_mutex_unlock(osal_mutex_t *mutex);

#endif /* OSAL_MUTEX_H */
