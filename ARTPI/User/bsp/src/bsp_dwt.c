#include "bsp.h"

#include <limits.h>

HAL_StatusTypeDef bsp_dwt_init(void)
{
    uint32_t start_cycles;

    SET_BIT(CoreDebug->DEMCR, CoreDebug_DEMCR_TRCENA_Msk);
    WRITE_REG(DWT->CYCCNT, 0U);
    SET_BIT(DWT->CTRL, DWT_CTRL_CYCCNTENA_Msk);

    start_cycles = DWT->CYCCNT;
    __NOP();
    __NOP();
    __NOP();

    return (DWT->CYCCNT != start_cycles) ? HAL_OK : HAL_ERROR;
}

uint32_t bsp_dwt_get_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t bsp_dwt_elapsed_cycles(uint32_t start_cycles)
{
    return DWT->CYCCNT - start_cycles;
}

void bsp_dwt_delay_cycles(uint32_t delay_cycles)
{
    uint32_t start_cycles = DWT->CYCCNT;

    while ((DWT->CYCCNT - start_cycles) < delay_cycles)
    {
    }
}

void bsp_dwt_delay_us(uint32_t delay_us)
{
    uint32_t cycles_per_us;
    uint32_t maximum_delay_us;

    if (delay_us == 0U)
    {
        return;
    }

    cycles_per_us = SystemCoreClock / 1000000U;
    if (cycles_per_us == 0U)
    {
        return;
    }

    maximum_delay_us = UINT32_MAX / cycles_per_us;
    while (delay_us > maximum_delay_us)
    {
        bsp_dwt_delay_cycles(maximum_delay_us * cycles_per_us);
        delay_us -= maximum_delay_us;
    }

    bsp_dwt_delay_cycles(delay_us * cycles_per_us);
}

void bsp_dwt_delay_ms(uint32_t delay_ms)
{
    while (delay_ms >= 1000U)
    {
        bsp_dwt_delay_us(1000000U);
        delay_ms -= 1000U;
    }

    bsp_dwt_delay_us(delay_ms * 1000U);
}
