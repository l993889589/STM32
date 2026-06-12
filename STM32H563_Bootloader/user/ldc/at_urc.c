#include "at_urc.h"

#include <string.h>

void at_urc_init(at_urc_table_t *table)
{
    if(!table)
        return;

    table->count = 0U;
}

int at_urc_register(at_urc_table_t *table, const char *prefix, at_urc_handler_t handler, void *arg)
{
    if(!table || !prefix || !handler)
        return -1;

    if(table->count >= AT_URC_MAX_ENTRIES)
        return -2;

    table->entries[table->count].prefix = prefix;
    table->entries[table->count].handler = handler;
    table->entries[table->count].arg = arg;
    table->count++;

    return 0;
}

int at_urc_dispatch(at_urc_table_t *table, const char *line)
{
    if(!table || !line)
        return 0;

    for(uint8_t i = 0U; i < table->count; i++)
    {
        const char *prefix = table->entries[i].prefix;
        size_t prefix_len = strlen(prefix);

        if(strncmp(line, prefix, prefix_len) == 0)
        {
            table->entries[i].handler(line, table->entries[i].arg);
            return 1;
        }
    }

    return 0;
}
