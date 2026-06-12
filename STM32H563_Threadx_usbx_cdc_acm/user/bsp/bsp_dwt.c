#include "bsp_dwt.h"

#include "main.h"

#define BSP_DWT_LAR_UNLOCK      0xC5ACCE55UL

bool bsp_dwt_init(void)
{
#if defined(DWT) && defined(CoreDebug_DEMCR_TRCENA_Msk)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

#if defined(DWT_LAR)
    DWT->LAR = BSP_DWT_LAR_UNLOCK;
#endif

    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
#else
    return false;
#endif
}

bool bsp_dwt_is_enabled(void)
{
#if defined(DWT)
    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
#else
    return false;
#endif
}

uint32_t bsp_dwt_get_cycle(void)
{
#if defined(DWT)
    return DWT->CYCCNT;
#else
    return 0U;
#endif
}

uint32_t bsp_dwt_elapsed_cycles(uint32_t start)
{
    return bsp_dwt_get_cycle() - start;
}

uint32_t bsp_dwt_us_to_cycles(uint32_t us)
{
    uint64_t cycles = ((uint64_t)SystemCoreClock * (uint64_t)us) / 1000000ULL;

    if(cycles > 0xFFFFFFFFULL)
        return 0xFFFFFFFFUL;

    return (uint32_t)cycles;
}

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

void bsp_dwt_delay_us(uint32_t us)
{
    bsp_dwt_delay_cycles(bsp_dwt_us_to_cycles(us));
}

void bsp_dwt_delay_ms(uint32_t ms)
{
    while(ms-- != 0U)
        bsp_dwt_delay_us(1000U);
}
