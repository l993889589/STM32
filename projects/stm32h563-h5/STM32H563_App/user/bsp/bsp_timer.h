#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t start_ms;
    uint32_t period_ms;
    bool active;
} bsp_timer_t;

void bsp_timer_init(void);
uint32_t bsp_timer_get_ms(void);
void bsp_timer_delay_ms(uint32_t ms);
void bsp_timer_start(bsp_timer_t *timer, uint32_t period_ms);
void bsp_timer_stop(bsp_timer_t *timer);
bool bsp_timer_expired(const bsp_timer_t *timer);
bool bsp_timer_poll(bsp_timer_t *timer);
uint32_t bsp_timer_elapsed_ms(uint32_t start_ms);

#endif /* BSP_TIMER_H */
