#ifndef BSP_DWT_H
#define BSP_DWT_H

#include <stdbool.h>
#include <stdint.h>

bool bsp_dwt_init(void);
bool bsp_dwt_is_enabled(void);
uint32_t bsp_dwt_get_cycle(void);
uint32_t bsp_dwt_get_us(void);
uint32_t bsp_dwt_elapsed_cycles(uint32_t start);
uint32_t bsp_dwt_us_to_cycles(uint32_t us);
void bsp_dwt_delay_cycles(uint32_t cycles);
void bsp_dwt_delay_us(uint32_t us);
void bsp_dwt_delay_ms(uint32_t ms);

#endif /* BSP_DWT_H */
