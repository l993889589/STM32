#include "ldc_proto_dispatcher.h"

void ldc_proto_dispatcher_init(ldc_proto_dispatcher_t *dispatcher,
                               ldc_proto_entry_t *entries,
                               uint8_t capacity)
{
    if(!dispatcher)
        return;

    dispatcher->entries = entries;
    dispatcher->capacity = capacity;
    dispatcher->count = 0U;
}

int ldc_proto_dispatcher_register(ldc_proto_dispatcher_t *dispatcher,
                                  ldc_proto_handler_t handler,
                                  void *arg)
{
    if(!dispatcher || !dispatcher->entries || !handler)
        return -1;

    if(dispatcher->count >= dispatcher->capacity)
        return -2;

    dispatcher->entries[dispatcher->count].handler = handler;
    dispatcher->entries[dispatcher->count].arg = arg;
    dispatcher->count++;

    return 0;
}

int ldc_proto_dispatcher_dispatch(ldc_proto_dispatcher_t *dispatcher,
                                  const uint8_t *data,
                                  uint32_t len)
{
    if(!dispatcher || !data || len == 0U)
        return 0;

    for(uint8_t i = 0U; i < dispatcher->count; i++)
    {
        /* First handler that claims the frame wins. */
        if(dispatcher->entries[i].handler &&
           dispatcher->entries[i].handler(data, len, dispatcher->entries[i].arg) == LDC_PROTO_HANDLED)
        {
            return 1;
        }
    }

    return 0;
}
