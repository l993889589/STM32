#ifndef APP_RS485_H
#define APP_RS485_H

#include "modbus.h"
#include "tx_api.h"

UINT app_rs485_init(void);
void app_rs485_task_entry(ULONG thread_input);
void app_rs485_get_stats(modbus_stats_t *stats);

#endif
