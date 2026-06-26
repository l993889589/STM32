#ifndef APP_MSG_BUS_SERVICE_H
#define APP_MSG_BUS_SERVICE_H

#include "app_msg_bus.h"
#include "tx_api.h"

UINT app_msg_bus_service_init(void);
bool app_msg_bus_service_publish(const app_msg_t *msg, app_msg_priority_t priority);
app_msg_bus_t *app_msg_bus_default(void);
void app_msg_bus_service_get_stats(app_msg_bus_stats_t *stats);
void app_msg_bus_task_entry(ULONG thread_input);

#endif /* APP_MSG_BUS_SERVICE_H */
