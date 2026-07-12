#ifndef AT_URC_H
#define AT_URC_H

#include <stdint.h>

#ifndef AT_URC_MAX_ENTRIES
#define AT_URC_MAX_ENTRIES 8U
#endif

typedef void (*at_urc_handler_t)(const char *line, void *arg);

typedef struct
{
    const char *prefix;
    at_urc_handler_t handler;
    void *arg;
} at_urc_entry_t;

typedef struct
{
    at_urc_entry_t entries[AT_URC_MAX_ENTRIES];
    uint8_t count;
} at_urc_table_t;

void at_urc_init(at_urc_table_t *table);
int at_urc_register(at_urc_table_t *table, const char *prefix, at_urc_handler_t handler, void *arg);
int at_urc_dispatch(at_urc_table_t *table, const char *line);

#endif
