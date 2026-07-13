#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include "stm32h7xx_hal.h"
#include "bsp_led.h"
#include "bsp_uart.h"

#define BSP_ERROR() bsp_error_handler(__FILE__, __LINE__)

void system_init(void);
void bsp_init(void);
void bsp_delay_ms(uint32_t delay_ms);
void bsp_error_handler(const char *file, uint32_t line);

#endif

