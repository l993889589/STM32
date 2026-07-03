#ifndef APP_UART4_CONSOLE_H
#define APP_UART4_CONSOLE_H

#include <stdint.h>

#include "ldc_queue.h"
#include "tx_api.h"

UINT app_uart4_console_init(void);
int app_uart4_console_write(const uint8_t *data, uint16_t length);
int app_uart4_console_write_string(const char *text);
int app_uart4_console_printf(const char *fmt, ...);
ldc_queue_t *app_uart4_console_queue(void);

#endif /* APP_UART4_CONSOLE_H */
