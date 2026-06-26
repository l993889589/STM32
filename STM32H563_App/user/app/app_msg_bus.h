#ifndef APP_MSG_BUS_H
#define APP_MSG_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_MSG_ANY_SOURCE      0xFFFFU
#define APP_MSG_ANY_TYPE        0xFFFFU

typedef enum
{
    APP_MSG_SOURCE_SYSTEM = 0,
    APP_MSG_SOURCE_USB,
    APP_MSG_SOURCE_RS485,
    APP_MSG_SOURCE_W800,
    APP_MSG_SOURCE_NEARLINK,
    APP_MSG_SOURCE_SHELL,
    APP_MSG_SOURCE_LOG,
    APP_MSG_SOURCE_USER = 0x8000U
} app_msg_source_t;

typedef enum
{
    APP_MSG_TYPE_CONTROL = 1,
    APP_MSG_TYPE_LINK_FRAME,
    APP_MSG_TYPE_LINK_ACTIVITY,
    APP_MSG_TYPE_LINK_OVERFLOW,
    APP_MSG_TYPE_LINK_DROP,
    APP_MSG_TYPE_STATUS_REQUEST,
    APP_MSG_TYPE_STATUS_RESPONSE,
    APP_MSG_TYPE_LOG_LINE,
    APP_MSG_TYPE_USER = 0x8000U
} app_msg_type_t;

typedef enum
{
    APP_MSG_PRIORITY_NORMAL = 0,
    APP_MSG_PRIORITY_HIGH
} app_msg_priority_t;

typedef struct
{
    uint16_t type;
    uint16_t source;
    uint16_t target;
    uint16_t length;
    const void *data;
    uintptr_t value;
} app_msg_t;

typedef void (*app_msg_handler_t)(const app_msg_t *msg, void *arg);

typedef struct
{
    uint16_t type;
    uint16_t source;
    app_msg_handler_t handler;
    void *arg;
} app_msg_subscription_t;

typedef struct
{
    uint32_t published;
    uint32_t dispatched;
    uint32_t dropped;
    uint32_t handler_calls;
    uint16_t high_used;
    uint16_t normal_used;
    uint16_t high_peak;
    uint16_t normal_peak;
} app_msg_bus_stats_t;

typedef struct
{
    app_msg_t *high_queue;
    uint16_t high_capacity;
    uint16_t high_in;
    uint16_t high_out;
    uint16_t high_count;

    app_msg_t *normal_queue;
    uint16_t normal_capacity;
    uint16_t normal_in;
    uint16_t normal_out;
    uint16_t normal_count;

    app_msg_subscription_t *subscriptions;
    uint16_t subscription_capacity;
    uint16_t subscription_count;

    app_msg_bus_stats_t stats;
} app_msg_bus_t;

bool app_msg_bus_init(app_msg_bus_t *bus,
                      app_msg_t *high_queue,
                      uint16_t high_capacity,
                      app_msg_t *normal_queue,
                      uint16_t normal_capacity,
                      app_msg_subscription_t *subscriptions,
                      uint16_t subscription_capacity);

bool app_msg_bus_subscribe(app_msg_bus_t *bus,
                           uint16_t type,
                           uint16_t source,
                           app_msg_handler_t handler,
                           void *arg);

bool app_msg_bus_publish(app_msg_bus_t *bus,
                         const app_msg_t *msg,
                         app_msg_priority_t priority);

bool app_msg_bus_receive(app_msg_bus_t *bus, app_msg_t *msg);
uint32_t app_msg_bus_dispatch(app_msg_bus_t *bus, const app_msg_t *msg);
bool app_msg_bus_dispatch_one(app_msg_bus_t *bus);
uint32_t app_msg_bus_dispatch_all(app_msg_bus_t *bus, uint32_t limit);
void app_msg_bus_get_stats(const app_msg_bus_t *bus, app_msg_bus_stats_t *stats);
bool app_msg_bus_has_pending(const app_msg_bus_t *bus);

#endif /* APP_MSG_BUS_H */
