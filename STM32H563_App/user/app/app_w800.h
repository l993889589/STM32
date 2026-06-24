#ifndef APP_W800_H
#define APP_W800_H

#include <stdint.h>

#include "tx_api.h"
#include "ldc_core.h"

typedef struct
{
    uint8_t wifi_ready;
    uint8_t mqtt_online;
    int socket_id;
    ldc_stats_t ldc;
} app_w800_status_t;

UINT app_w800_init(void);
void app_w800_task_entry(ULONG thread_input);
void app_w800_get_status(app_w800_status_t *status);
void app_w800_request_reconnect(void);

#endif
