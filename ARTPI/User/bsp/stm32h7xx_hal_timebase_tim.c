#include "bsp.h"
#include "tx_api.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    (void)tick_priority;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    static uint32_t last_cycles;
    static uint32_t cycle_remainder;
    static uint32_t pre_kernel_tick = 0U;
    static uint8_t pre_kernel_initialized;

    if (tx_thread_identify() != TX_NULL)
    {
        return (uint32_t)tx_time_get();
    }

    if (pre_kernel_initialized == 0U)
    {
        last_cycles = bsp_dwt_get_cycles();
        pre_kernel_initialized = 1U;
    }
    else
    {
        uint32_t cycles_per_ms = SystemCoreClock / 1000U;
        uint32_t now_cycles = bsp_dwt_get_cycles();
        uint64_t elapsed_cycles = (uint64_t)(now_cycles - last_cycles) + cycle_remainder;

        last_cycles = now_cycles;
        if (cycles_per_ms != 0U)
        {
            pre_kernel_tick += (uint32_t)(elapsed_cycles / cycles_per_ms);
            cycle_remainder = (uint32_t)(elapsed_cycles % cycles_per_ms);
        }
    }

    return pre_kernel_tick;
}

void HAL_Delay(uint32_t delay_ms)
{
    bsp_delay_ms(delay_ms);
}
