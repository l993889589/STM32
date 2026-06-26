#include "app_msg_bus_service.h"

#include <string.h>

#include "app_config.h"

#if APP_ENABLE_MSG_BUS

enum
{
    APP_MSG_BUS_EVT_PENDING = 1UL << 0
};

static app_msg_bus_t g_bus;
static app_msg_t g_high_queue[APP_MSG_BUS_HIGH_QUEUE_DEPTH];
static app_msg_t g_normal_queue[APP_MSG_BUS_NORMAL_QUEUE_DEPTH];
static app_msg_subscription_t g_subscriptions[APP_MSG_BUS_HANDLER_COUNT];
static TX_EVENT_FLAGS_GROUP g_events;
static uint8_t g_initialized;

static uint32_t app_msg_bus_lock(void)
{
    return (uint32_t)tx_interrupt_control(TX_INT_DISABLE);
}

static void app_msg_bus_unlock(uint32_t state)
{
    (void)tx_interrupt_control((UINT)state);
}

UINT app_msg_bus_service_init(void)
{
    UINT status;

    if(g_initialized != 0U)
        return TX_SUCCESS;

    if(!app_msg_bus_init(&g_bus,
                         g_high_queue,
                         APP_MSG_BUS_HIGH_QUEUE_DEPTH,
                         g_normal_queue,
                         APP_MSG_BUS_NORMAL_QUEUE_DEPTH,
                         g_subscriptions,
                         APP_MSG_BUS_HANDLER_COUNT))
        return TX_SIZE_ERROR;

    status = tx_event_flags_create(&g_events, "app msg bus");
    if(status != TX_SUCCESS)
        return status;

    g_initialized = 1U;
    return TX_SUCCESS;
}

bool app_msg_bus_service_subscribe(uint16_t type,
                                   uint16_t source,
                                   app_msg_handler_t handler,
                                   void *arg)
{
    bool ok;
    uint32_t state;

    if(g_initialized == 0U)
        return false;

    state = app_msg_bus_lock();
    ok = app_msg_bus_subscribe(&g_bus, type, source, handler, arg);
    app_msg_bus_unlock(state);
    return ok;
}

bool app_msg_bus_service_publish(const app_msg_t *msg, app_msg_priority_t priority)
{
    bool ok;
    uint32_t state;

    if(g_initialized == 0U)
        return false;

    state = app_msg_bus_lock();
    ok = app_msg_bus_publish(&g_bus, msg, priority);
    app_msg_bus_unlock(state);

    if(ok)
        (void)tx_event_flags_set(&g_events, APP_MSG_BUS_EVT_PENDING, TX_OR);
    return ok;
}

app_msg_bus_t *app_msg_bus_default(void)
{
    return (g_initialized != 0U) ? &g_bus : NULL;
}

void app_msg_bus_service_get_stats(app_msg_bus_stats_t *stats)
{
    uint32_t state;

    if(stats == NULL)
        return;

    memset(stats, 0, sizeof(*stats));
    if(g_initialized == 0U)
        return;

    state = app_msg_bus_lock();
    app_msg_bus_get_stats(&g_bus, stats);
    app_msg_bus_unlock(state);
}

void app_msg_bus_task_entry(ULONG thread_input)
{
    ULONG events;

    (void)thread_input;
    for(;;)
    {
        if(tx_event_flags_get(&g_events,
                              APP_MSG_BUS_EVT_PENDING,
                              TX_OR_CLEAR,
                              &events,
                              TX_WAIT_FOREVER) == TX_SUCCESS)
        {
            for(;;)
            {
                app_msg_t msg;
                uint32_t state;
                bool received;

                state = app_msg_bus_lock();
                received = app_msg_bus_receive(&g_bus, &msg);
                app_msg_bus_unlock(state);

                if(!received)
                    break;

                (void)app_msg_bus_dispatch(&g_bus, &msg);
            }
        }
    }
}

#else

UINT app_msg_bus_service_init(void)
{
    return TX_SUCCESS;
}

bool app_msg_bus_service_subscribe(uint16_t type,
                                   uint16_t source,
                                   app_msg_handler_t handler,
                                   void *arg)
{
    (void)type;
    (void)source;
    (void)handler;
    (void)arg;
    return false;
}

bool app_msg_bus_service_publish(const app_msg_t *msg, app_msg_priority_t priority)
{
    (void)msg;
    (void)priority;
    return false;
}

app_msg_bus_t *app_msg_bus_default(void)
{
    return NULL;
}

void app_msg_bus_service_get_stats(app_msg_bus_stats_t *stats)
{
    if(stats != NULL)
        memset(stats, 0, sizeof(*stats));
}

void app_msg_bus_task_entry(ULONG thread_input)
{
    (void)thread_input;
    for(;;)
        tx_thread_sleep(TX_WAIT_FOREVER);
}

#endif
