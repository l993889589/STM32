#ifndef BSP_UART_H
#define BSP_UART_H

#include <stddef.h>
#include <stdint.h>

void bsp_uart4_init(void);
void bsp_uart4_write(const uint8_t *data, size_t length);
void bsp_uart4_write_string(const char *text);

#endif

