#include "app_event_bridge.h"

#include "app_config.h"
#include "app_msg_bus_service.h"

static void app_event_publish(app_msg_source_t source,
                              app_msg_type_t type,
                              uintptr_t value,
                              uint16_t length,
                              app_msg_priority_t priority)
{
#if APP_ENABLE_MSG_BUS
    app_msg_t msg;

    msg.type = (uint16_t)type;
    msg.source = (uint16_t)source;
    msg.target = APP_MSG_ANY_SOURCE;
    msg.length = length;
    msg.data = NULL;
    msg.value = value;
    (void)app_msg_bus_service_publish(&msg, priority);
#else
    (void)source;
    (void)type;
    (void)value;
    (void)length;
    (void)priority;
#endif
}

void app_event_link_activity(app_msg_source_t source, uint16_t length)
{
    app_event_publish(source,
                      APP_MSG_TYPE_LINK_ACTIVITY,
                      (uintptr_t)length,
                      length,
                      APP_MSG_PRIORITY_NORMAL);
}

void app_event_link_frame(app_msg_source_t source, uint16_t length)
{
    app_event_publish(source,
                      APP_MSG_TYPE_LINK_FRAME,
                      (uintptr_t)length,
                      length,
                      APP_MSG_PRIORITY_NORMAL);
}

void app_event_status(app_msg_source_t source, uintptr_t value)
{
    app_event_publish(source,
                      APP_MSG_TYPE_STATUS_RESPONSE,
                      value,
                      0U,
                      APP_MSG_PRIORITY_NORMAL);
}

void app_event_error(app_msg_source_t source, uintptr_t value)
{
    app_event_publish(source,
                      APP_MSG_TYPE_LINK_DROP,
                      value,
                      0U,
                      APP_MSG_PRIORITY_HIGH);
}
