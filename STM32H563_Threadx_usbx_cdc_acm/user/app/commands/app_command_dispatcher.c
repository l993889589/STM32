#include "app_command_dispatcher.h"

#include <stddef.h>
#include <string.h>

void app_command_dispatcher_init(app_command_dispatcher_t *dispatcher,
                                 app_command_entry_t *entries,
                                 uint8_t capacity)
{
    if(!dispatcher)
        return;

    memset(dispatcher, 0, sizeof(*dispatcher));
    dispatcher->entries = entries;
    dispatcher->capacity = capacity;
}

int app_command_register(app_command_dispatcher_t *dispatcher,
                         uint16_t id,
                         app_command_handler_t handler,
                         void *arg)
{
    if(!dispatcher || !dispatcher->entries || !handler)
        return -1;

    for(uint8_t i = 0U; i < dispatcher->count; i++)
    {
        if(dispatcher->entries[i].id == id)
            return -2;
    }

    if(dispatcher->count >= dispatcher->capacity)
        return -3;

    dispatcher->entries[dispatcher->count].id = id;
    dispatcher->entries[dispatcher->count].handler = handler;
    dispatcher->entries[dispatcher->count].arg = arg;
    dispatcher->count++;

    return 0;
}

int app_command_dispatch(app_command_dispatcher_t *dispatcher, uint16_t id)
{
    if(!dispatcher || !dispatcher->entries)
        return -1;

    if(dispatcher->in_handler)
        return -2;

    for(uint8_t i = 0U; i < dispatcher->count; i++)
    {
        if(dispatcher->entries[i].id == id)
        {
            dispatcher->in_handler = 1U;
            dispatcher->entries[i].handler(dispatcher->entries[i].arg);
            dispatcher->in_handler = 0U;
            return 0;
        }
    }

    return -3;
}
