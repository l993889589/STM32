#ifdef NDEBUG
#undef NDEBUG
#endif

#include "app_msg_bus.h"

#include <assert.h>
#include <stdio.h>

typedef struct
{
    uint32_t calls;
    uintptr_t last_value;
} test_sink_t;

static void test_handler(const app_msg_t *msg, void *arg)
{
    test_sink_t *sink = (test_sink_t *)arg;

    assert(msg != NULL);
    assert(sink != NULL);
    sink->calls++;
    sink->last_value = msg->value;
}

static void test_priority_order(void)
{
    app_msg_t high_queue[2];
    app_msg_t normal_queue[4];
    app_msg_subscription_t subscriptions[2];
    app_msg_bus_t bus;
    test_sink_t sink = {0U, 0U};
    app_msg_t msg = {0};

    assert(app_msg_bus_init(&bus,
                            high_queue, 2U,
                            normal_queue, 4U,
                            subscriptions, 2U));
    assert(app_msg_bus_subscribe(&bus,
                                 APP_MSG_ANY_TYPE,
                                 APP_MSG_ANY_SOURCE,
                                 test_handler,
                                 &sink));

    msg.type = APP_MSG_TYPE_LOG_LINE;
    msg.source = APP_MSG_SOURCE_LOG;
    msg.value = 1U;
    assert(app_msg_bus_publish(&bus, &msg, APP_MSG_PRIORITY_NORMAL));

    msg.type = APP_MSG_TYPE_CONTROL;
    msg.source = APP_MSG_SOURCE_SHELL;
    msg.value = 2U;
    assert(app_msg_bus_publish(&bus, &msg, APP_MSG_PRIORITY_HIGH));

    assert(app_msg_bus_dispatch_one(&bus));
    assert(sink.calls == 1U);
    assert(sink.last_value == 2U);
    assert(app_msg_bus_dispatch_one(&bus));
    assert(sink.calls == 2U);
    assert(sink.last_value == 1U);
    assert(!app_msg_bus_dispatch_one(&bus));
}

static void test_filter_and_overflow_stats(void)
{
    app_msg_t high_queue[1];
    app_msg_t normal_queue[1];
    app_msg_subscription_t subscriptions[2];
    app_msg_bus_t bus;
    app_msg_bus_stats_t stats;
    test_sink_t sink = {0U, 0U};
    app_msg_t msg = {0};

    assert(app_msg_bus_init(&bus,
                            high_queue, 1U,
                            normal_queue, 1U,
                            subscriptions, 2U));
    assert(app_msg_bus_subscribe(&bus,
                                 APP_MSG_TYPE_STATUS_REQUEST,
                                 APP_MSG_SOURCE_SHELL,
                                 test_handler,
                                 &sink));

    msg.type = APP_MSG_TYPE_LOG_LINE;
    msg.source = APP_MSG_SOURCE_LOG;
    msg.value = 10U;
    assert(app_msg_bus_publish(&bus, &msg, APP_MSG_PRIORITY_NORMAL));

    msg.value = 11U;
    assert(!app_msg_bus_publish(&bus, &msg, APP_MSG_PRIORITY_NORMAL));

    assert(app_msg_bus_dispatch_all(&bus, 0U) == 1U);
    assert(sink.calls == 0U);

    msg.type = APP_MSG_TYPE_STATUS_REQUEST;
    msg.source = APP_MSG_SOURCE_SHELL;
    msg.value = 12U;
    assert(app_msg_bus_publish(&bus, &msg, APP_MSG_PRIORITY_NORMAL));
    assert(app_msg_bus_dispatch_all(&bus, 0U) == 1U);
    assert(sink.calls == 1U);
    assert(sink.last_value == 12U);

    app_msg_bus_get_stats(&bus, &stats);
    assert(stats.published == 2U);
    assert(stats.dispatched == 2U);
    assert(stats.dropped == 1U);
    assert(stats.handler_calls == 1U);
    assert(stats.normal_peak == 1U);
}

int main(void)
{
    test_priority_order();
    test_filter_and_overflow_stats();
    (void)printf("app_msg_bus tests passed\n");
    return 0;
}
