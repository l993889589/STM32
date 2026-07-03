#include "bsp_timer.h"

#include "main.h"

void bsp_timer_init(void)
{
}

uint32_t bsp_timer_get_ms(void)
{
    return HAL_GetTick();
}

void bsp_timer_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void bsp_timer_start(bsp_timer_t *timer, uint32_t period_ms)
{
    if(!timer)
        return;

    timer->start_ms = bsp_timer_get_ms();
    timer->period_ms = period_ms;
    timer->active = true;
}

void bsp_timer_stop(bsp_timer_t *timer)
{
    if(!timer)
        return;

    timer->active = false;
}

uint32_t bsp_timer_elapsed_ms(uint32_t start_ms)
{
    return bsp_timer_get_ms() - start_ms;
}

bool bsp_timer_expired(const bsp_timer_t *timer)
{
    if(!timer || !timer->active)
        return false;

    return bsp_timer_elapsed_ms(timer->start_ms) >= timer->period_ms;
}

bool bsp_timer_poll(bsp_timer_t *timer)
{
    if(!bsp_timer_expired(timer))
        return false;

    timer->start_ms += timer->period_ms;
    if(bsp_timer_elapsed_ms(timer->start_ms) >= timer->period_ms)
        timer->start_ms = bsp_timer_get_ms();

    return true;
}
