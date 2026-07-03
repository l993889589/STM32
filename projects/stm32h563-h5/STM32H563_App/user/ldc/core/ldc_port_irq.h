#ifndef LDC_PORT_IRQ_H
#define LDC_PORT_IRQ_H

#include <stdint.h>

#include "cmsis_compiler.h"

/*
 * Platform critical section for bare-metal/ISR-safe LDC access.
 *
 * This file intentionally lives outside the LDC core. It uses CMSIS IRQ
 * primitives, so include it from STM32-side adapter code, not from ldc_core.c.
 */
static inline uint32_t ldc_port_irq_lock(void *arg)
{
    uint32_t state;

    (void)arg;
    state = __get_PRIMASK();
    __disable_irq();
    return state;
}

static inline void ldc_port_irq_unlock(void *arg, uint32_t state)
{
    (void)arg;
    __set_PRIMASK(state);
}

#endif /* LDC_PORT_IRQ_H */
