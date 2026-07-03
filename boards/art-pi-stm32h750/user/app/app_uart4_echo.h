#ifndef APP_UART4_ECHO_H
#define APP_UART4_ECHO_H

#include "ldc_queue.h"
#include "tx_api.h"

UINT app_uart4_echo_init(void);
ldc_queue_t *app_uart4_echo_queue(void);

#endif /* APP_UART4_ECHO_H */
