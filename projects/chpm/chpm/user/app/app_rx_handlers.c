#include "app_rx_handlers.h"

#include "app_main.h"

void dwin_uart_queue_handler(unsigned char *message, int length)
{
    msg_analysis(message, length);
}
