/**
 * @file bsp_dwt.c
 * @brief DWT cycle counter and short busy-wait helpers.
 *
 * bsp_dwt_init() enables CYCCNT during BSP startup. Use bsp_dwt_get_cycle()
 * when raw cycle deltas are needed, or bsp_dwt_get_us() when a monotonic
 * microsecond timestamp is required by higher layers. The RS485 LDC service
 * uses bsp_dwt_get_us() to measure Modbus RTU silent gaps.
 *
 * Delay helpers are busy-wait utilities for short board bring-up delays. Do
 * not use them for long waits inside application tasks.
 */
#include "bsp_dwt.h"

#include "bsp_irq_lock.h"
#include "stm32h563xx.h"

#define BSP_DWT_LAR_UNLOCK      0xC5ACCE55UL

static uint32_t g_dwt_last_cycle;
static uint64_t g_dwt_accumulated_cycles;

/** @brief Enable and reset the Cortex-M33 DWT cycle counter. */
bool bsp_dwt_init(void)
{
#if defined(DWT) && defined(CoreDebug_DEMCR_TRCENA_Msk)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#if defined(DWT_LAR)
    DWT->LAR = BSP_DWT_LAR_UNLOCK;
#endif

    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_dwt_last_cycle = DWT->CYCCNT;
    g_dwt_accumulated_cycles = 0ULL;

    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
#else
    return false;
#endif
}

/** @brief Report whether the DWT cycle counter is running. */
bool bsp_dwt_is_enabled(void)
{
#if defined(DWT)
    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
#else
    return false;
#endif
}

/** @brief Return the current wrapping DWT cycle count. */
uint32_t bsp_dwt_get_cycle(void)
{
#if defined(DWT)
    return DWT->CYCCNT;
#else
    return 0U;
#endif
}

/**
 * @brief Return a true 32-bit wrapping microsecond timestamp.
 *
 * CYCCNT itself wraps in roughly 17 seconds at 250 MHz. Converting the raw
 * counter before extending that wrap creates a false multi-billion-us delta.
 * Accumulate wrap-safe cycle deltas first, then convert the 64-bit total. This
 * function is called by periodic CAN and RS-485 services more often than one
 * raw CYCCNT period; the short IRQ lock also serializes callers across tasks.
 */
uint32_t bsp_dwt_get_us(void)
{
    uint32_t clock = SystemCoreClock;
    uint32_t current;
    uint32_t delta;
    uint64_t cycles;
    uint64_t seconds;
    uint64_t remainder;
    uint64_t microseconds;
    bsp_irq_state_t irq_state;

    if(clock == 0U)
        return 0U;

    irq_state = bsp_irq_lock();
    current = bsp_dwt_get_cycle();
    delta = current - g_dwt_last_cycle;
    g_dwt_last_cycle = current;
    g_dwt_accumulated_cycles += delta;
    cycles = g_dwt_accumulated_cycles;
    bsp_irq_unlock(irq_state);

    seconds = cycles / (uint64_t)clock;
    remainder = cycles % (uint64_t)clock;
    microseconds = (seconds * 1000000ULL) +
                   ((remainder * 1000000ULL) / (uint64_t)clock);
    return (uint32_t)microseconds;
}

/** @brief Calculate a wrap-safe cycle delta. */
uint32_t bsp_dwt_elapsed_cycles(uint32_t start)
{
    return bsp_dwt_get_cycle() - start;
}

/** @brief Convert microseconds to core cycles with saturation. */
uint32_t bsp_dwt_us_to_cycles(uint32_t us)
{
    uint64_t cycles = ((uint64_t)SystemCoreClock * (uint64_t)us) / 1000000ULL;

    if(cycles > 0xFFFFFFFFULL)
        return 0xFFFFFFFFUL;

    return (uint32_t)cycles;
}

/** @brief Busy-wait for a bounded number of core cycles. */
void bsp_dwt_delay_cycles(uint32_t cycles)
{
    uint32_t start;

    if(cycles == 0U)
        return;

    if(!bsp_dwt_is_enabled() && !bsp_dwt_init())
        return;

    start = bsp_dwt_get_cycle();
    while(bsp_dwt_elapsed_cycles(start) < cycles)
    {
        __NOP();
    }
}

/** @brief Busy-wait for a short microsecond interval. */
void bsp_dwt_delay_us(uint32_t us)
{
    bsp_dwt_delay_cycles(bsp_dwt_us_to_cycles(us));
}

/** @brief Busy-wait for a millisecond interval. */
void bsp_dwt_delay_ms(uint32_t ms)
{
    while(ms-- != 0U)
        bsp_dwt_delay_us(1000U);
}
