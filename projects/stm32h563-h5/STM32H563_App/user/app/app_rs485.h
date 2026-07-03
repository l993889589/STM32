#ifndef APP_RS485_H
#define APP_RS485_H

#include <stdbool.h>
#include <stdint.h>

#include "modbus.h"
#include "ldc_core.h"
#include "tx_api.h"

typedef enum
{
    APP_RS485_NET_STATUS = 0,
    APP_RS485_NET_CONFIG,
    APP_RS485_NET_DATA
} app_rs485_net_payload_t;

UINT app_rs485_init(void);
void app_rs485_task_entry(ULONG thread_input);
void app_rs485_get_stats(modbus_stats_t *stats);
bool app_rs485_get_ldc_stats(ldc_stats_t *stats);
uint8_t app_rs485_unit_id(void);
int app_rs485_format_network_payload(app_rs485_net_payload_t type, char *out, uint16_t out_size);
bool app_rs485_apply_network_command(const char *command);

#endif
