#ifndef APP_SERVICES_H
#define APP_SERVICES_H

#include <stdint.h>

#include "bsp_status.h"

typedef void (*app_services_tick_hook_t)(void);

typedef struct
{
    uint32_t modbus_baud_rate;
    app_services_tick_hook_t tick_1ms_hook;
} app_services_config_t;

bsp_status_t app_services_init(const app_services_config_t *config);
void app_services_tick_1ms(void);

#endif
