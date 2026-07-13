#include "bsp.h"
#include "tx_api.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    (void)tick_priority;
    return HAL_OK;
}

uint32_t HAL_GetTick(void)
{
    static uint32_t pre_kernel_tick = 0U;

    if (tx_thread_identify() != TX_NULL)
    {
        return (uint32_t)tx_time_get();
    }

    for (volatile uint32_t delay = (SystemCoreClock >> 14U); delay > 0U; delay--)
    {
        __NOP();
    }

    pre_kernel_tick++;
    return pre_kernel_tick;
}

void HAL_Delay(uint32_t delay_ms)
{
    bsp_delay_ms(delay_ms);
}
