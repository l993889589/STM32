#include "app_msg_bus.h"

#include <string.h>

static bool app_msg_bus_push(app_msg_t *queue,
                             uint16_t capacity,
                             uint16_t *in,
                             uint16_t *out,
                             uint16_t *count,
                             const app_msg_t *msg,
                             app_msg_full_policy_t policy,
                             app_msg_bus_stats_t *stats)
{
    if(queue == NULL || capacity == 0U || in == NULL || count == NULL ||
       out == NULL || msg == NULL || stats == NULL)
        return false;

    if(*count >= capacity)
    {
        if(policy == APP_MSG_DROP_NEWEST)
        {
            stats->dropped++;
            return false;
        }

        /*
         * APP_MSG_FORCE_HIGH_PRIORITY is intentionally treated as
         * drop-oldest on the selected priority queue. The bus core does not
         * move messages between queues; applications choose queue sizes and
         * policy according to the severity of their events.
         */
        *out = (uint16_t)((*out + 1U) % capacity);
        (*count)--;
        stats->overwritten++;
    }

    queue[*in] = *msg;
    *in = (uint16_t)((*in + 1U) % capacity);
    (*count)++;
    return true;
}

static bool app_msg_bus_pop(app_msg_t *queue,
                            uint16_t capacity,
                            uint16_t *out,
                            uint16_t *count,
                            app_msg_t *msg)
{
    if(queue == NULL || capacity == 0U || out == NULL || count == NULL ||
       msg == NULL || *count == 0U)
        return false;

    *msg = queue[*out];
    *out = (uint16_t)((*out + 1U) % capacity);
    (*count)--;
    return true;
}

static bool app_msg_matches(const app_msg_subscription_t *sub, const app_msg_t *msg)
{
    if(sub == NULL || msg == NULL || sub->handler == NULL)
        return false;

    if(sub->type != APP_MSG_ANY_TYPE && sub->type != msg->type)
        return false;

    if(sub->source != APP_MSG_ANY_SOURCE && sub->source != msg->source)
        return false;

    return true;
}

bool app_msg_bus_init_with_config(app_msg_bus_t *bus,
                                  const app_msg_bus_config_t *config)
{
    if(bus == NULL || config == NULL ||
       config->high_queue == NULL || config->high_capacity == 0U ||
       config->normal_queue == NULL || config->normal_capacity == 0U ||
       config->subscriptions == NULL || config->subscription_capacity == 0U)
        return false;

    memset(bus, 0, sizeof(*bus));
    bus->high_queue = config->high_queue;
    bus->high_capacity = config->high_capacity;
    bus->normal_queue = config->normal_queue;
    bus->normal_capacity = config->normal_capacity;
    bus->subscriptions = config->subscriptions;
    bus->subscription_capacity = config->subscription_capacity;
    bus->high_full_policy = config->high_full_policy;
    bus->normal_full_policy = config->normal_full_policy;
    return true;
}

bool app_msg_bus_init(app_msg_bus_t *bus,
                      app_msg_t *high_queue,
                      uint16_t high_capacity,
                      app_msg_t *normal_queue,
                      uint16_t normal_capacity,
                      app_msg_subscription_t *subscriptions,
                      uint16_t subscription_capacity)
{
    app_msg_bus_config_t config;

    config.high_queue = high_queue;
    config.high_capacity = high_capacity;
    config.normal_queue = normal_queue;
    config.normal_capacity = normal_capacity;
    config.subscriptions = subscriptions;
    config.subscription_capacity = subscription_capacity;
    config.high_full_policy = APP_MSG_DROP_NEWEST;
    config.normal_full_policy = APP_MSG_DROP_NEWEST;
    return app_msg_bus_init_with_config(bus, &config);
}

bool app_msg_bus_subscribe(app_msg_bus_t *bus,
                           uint16_t type,
                           uint16_t source,
                           app_msg_handler_t handler,
                           void *arg)
{
    app_msg_subscription_t *sub;

    if(bus == NULL || handler == NULL ||
       bus->subscription_count >= bus->subscription_capacity)
        return false;

    sub = &bus->subscriptions[bus->subscription_count++];
    sub->type = type;
    sub->source = source;
    sub->handler = handler;
    sub->arg = arg;
    return true;
}

bool app_msg_bus_publish(app_msg_bus_t *bus,
                         const app_msg_t *msg,
                         app_msg_priority_t priority)
{
    bool ok;
    app_msg_t queued_msg;

    if(bus == NULL || msg == NULL)
        return false;

    queued_msg = *msg;
    if(priority == APP_MSG_PRIORITY_HIGH)
        queued_msg.flags |= APP_MSG_FLAG_HIGH;

    if(priority == APP_MSG_PRIORITY_HIGH)
    {
        ok = app_msg_bus_push(bus->high_queue,
                              bus->high_capacity,
                              &bus->high_in,
                              &bus->high_out,
                              &bus->high_count,
                              &queued_msg,
                              bus->high_full_policy,
                              &bus->stats);
        if(ok && bus->high_count > bus->stats.high_peak)
            bus->stats.high_peak = bus->high_count;
    }
    else
    {
        ok = app_msg_bus_push(bus->normal_queue,
                              bus->normal_capacity,
                              &bus->normal_in,
                              &bus->normal_out,
                              &bus->normal_count,
                              &queued_msg,
                              bus->normal_full_policy,
                              &bus->stats);
        if(ok && bus->normal_count > bus->stats.normal_peak)
            bus->stats.normal_peak = bus->normal_count;
    }

    if(ok)
        bus->stats.published++;

    bus->stats.high_used = bus->high_count;
    bus->stats.normal_used = bus->normal_count;
    return ok;
}

bool app_msg_bus_receive(app_msg_bus_t *bus, app_msg_t *msg)
{
    bool got;

    if(bus == NULL || msg == NULL)
        return false;

    got = app_msg_bus_pop(bus->high_queue,
                          bus->high_capacity,
                          &bus->high_out,
                          &bus->high_count,
                          msg);
    if(!got)
        got = app_msg_bus_pop(bus->normal_queue,
                              bus->normal_capacity,
                              &bus->normal_out,
                              &bus->normal_count,
                              msg);
    if(!got)
        return false;

    bus->stats.dispatched++;
    bus->stats.received++;
    bus->stats.high_used = bus->high_count;
    bus->stats.normal_used = bus->normal_count;
    return true;
}

uint32_t app_msg_bus_dispatch(app_msg_bus_t *bus, const app_msg_t *msg)
{
    uint32_t calls = 0U;

    if(bus == NULL || msg == NULL)
        return 0U;

    for(uint16_t i = 0U; i < bus->subscription_count; i++)
    {
        app_msg_subscription_t *sub = &bus->subscriptions[i];
        if(app_msg_matches(sub, msg))
        {
            sub->handler(msg, sub->arg);
            bus->stats.handler_calls++;
            calls++;
        }
    }

    if(calls == 0U)
        bus->stats.dispatch_no_handler++;

    return calls;
}

bool app_msg_bus_dispatch_one(app_msg_bus_t *bus)
{
    app_msg_t msg;

    if(!app_msg_bus_receive(bus, &msg))
        return false;

    (void)app_msg_bus_dispatch(bus, &msg);
    return true;
}

uint32_t app_msg_bus_dispatch_all(app_msg_bus_t *bus, uint32_t limit)
{
    uint32_t count = 0U;

    if(bus == NULL)
        return 0U;

    while((limit == 0U || count < limit) && app_msg_bus_dispatch_one(bus))
        count++;

    return count;
}

void app_msg_bus_get_stats(const app_msg_bus_t *bus, app_msg_bus_stats_t *stats)
{
    if(bus == NULL || stats == NULL)
        return;

    *stats = bus->stats;
}

bool app_msg_bus_has_pending(const app_msg_bus_t *bus)
{
    return bus != NULL && (bus->high_count != 0U || bus->normal_count != 0U);
}
