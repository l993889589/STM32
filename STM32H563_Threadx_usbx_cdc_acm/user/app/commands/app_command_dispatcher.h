#ifndef APP_COMMAND_DISPATCHER_H
#define APP_COMMAND_DISPATCHER_H

#include <stdint.h>

typedef void (*app_command_handler_t)(void *arg);

typedef struct
{
    uint16_t id;
    app_command_handler_t handler;
    void *arg;
} app_command_entry_t;

typedef struct
{
    app_command_entry_t *entries;
    uint8_t capacity;
    uint8_t count;
    uint8_t in_handler;
} app_command_dispatcher_t;

void app_command_dispatcher_init(app_command_dispatcher_t *dispatcher,
                                 app_command_entry_t *entries,
                                 uint8_t capacity);
int app_command_register(app_command_dispatcher_t *dispatcher,
                         uint16_t id,
                         app_command_handler_t handler,
                         void *arg);
int app_command_dispatch(app_command_dispatcher_t *dispatcher, uint16_t id);

#endif /* APP_COMMAND_DISPATCHER_H */
