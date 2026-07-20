/**
 * @file bsp_dwt.c
 * @brief STM32F401 DWT cycle counter and wrap-safe delay implementation.
 */

#include "bsp_dwt.h"

#include <stdbool.h>

#include "stm32f4xx.h"

static bool dwt_initialized;

/** @brief Enable trace and the Cortex-M4 DWT cycle counter. */
bsp_status_t bsp_dwt_init(void)
{
    if(dwt_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
        return BSP_STATUS_NOT_SUPPORTED;

    dwt_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Return the wrapping DWT cycle counter. */
uint32_t bsp_dwt_get_cycles(void)
{
    return dwt_initialized ? DWT->CYCCNT : 0U;
}

/** @brief Return the DWT frequency, which equals the Cortex-M4 core clock. */
uint32_t bsp_dwt_frequency_hz(void)
{
    return dwt_initialized ? SystemCoreClock : 0U;
}

/** @brief Busy-wait for a wrap-safe interval of core clock cycles. */
void bsp_dwt_delay_cycles(uint32_t cycles)
{
    uint32_t start;

    if(!dwt_initialized || cycles == 0U)
        return;
    start = DWT->CYCCNT;
    while((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
    }
}

/** @brief Busy-wait for a microsecond interval without a HAL tick dependency. */
void bsp_dwt_delay_us(uint32_t delay_us)
{
    uint64_t remaining_cycles;
    uint32_t cycles_per_us;

    if(!dwt_initialized || delay_us == 0U)
        return;
    cycles_per_us = SystemCoreClock / 1000000U;
    if(cycles_per_us == 0U)
        return;
    remaining_cycles = (uint64_t)cycles_per_us * delay_us;
    while(remaining_cycles != 0U)
    {
        uint32_t chunk = remaining_cycles > 0xFFFFFFFFULL ?
                         0xFFFFFFFFUL : (uint32_t)remaining_cycles;
        bsp_dwt_delay_cycles(chunk);
        remaining_cycles -= chunk;
    }
}

/** @brief Busy-wait for a millisecond interval using the DWT counter. */
void bsp_dwt_delay_ms(uint32_t delay_ms)
{
    while(delay_ms-- != 0U)
        bsp_dwt_delay_us(1000U);
}
