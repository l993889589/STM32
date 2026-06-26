#ifndef APP_EVENT_BRIDGE_H
#define APP_EVENT_BRIDGE_H

#include <stdint.h>

#include "app_msg_bus.h"

void app_event_link_activity(app_msg_source_t source, uint16_t length);
void app_event_link_frame(app_msg_source_t source, uint16_t length);
void app_event_status(app_msg_source_t source, uintptr_t value);
void app_event_error(app_msg_source_t source, uintptr_t value);

#endif /* APP_EVENT_BRIDGE_H */
