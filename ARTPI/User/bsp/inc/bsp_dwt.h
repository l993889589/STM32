#ifndef BSP_DWT_H
#define BSP_DWT_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

HAL_StatusTypeDef bsp_dwt_init(void);
uint32_t bsp_dwt_get_cycles(void);
uint32_t bsp_dwt_elapsed_cycles(uint32_t start_cycles);

/* DWT delays occupy the caller; interrupt handling remains enabled by default. */
void bsp_dwt_delay_cycles(uint32_t delay_cycles);
void bsp_dwt_delay_us(uint32_t delay_us);
void bsp_dwt_delay_ms(uint32_t delay_ms);

#endif
