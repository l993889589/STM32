#ifndef BOOT_UART_H
#define BOOT_UART_H

#include "stm32h7xx_hal.h"

HAL_StatusTypeDef boot_uart_init(void);
void boot_uart_write(const char *text);

#endif
