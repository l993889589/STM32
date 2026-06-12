#include "dispatcher.h"
#include <stdbool.h>

static dispatcher_entry_t g_dispatcher_table[MAX_DISPATCHER_COUNT];
static uint8_t g_dispatcher_count = 0;

int dispatcher_register(uint16_t cmd, dispatcher_handler_t handler)
{
    for (uint8_t i = 0; i < g_dispatcher_count; i++) {
        if (g_dispatcher_table[i].cmd == cmd)
            return -2; // “—◊¢≤·
    }

    if (g_dispatcher_count >= MAX_DISPATCHER_COUNT)
        return -1;

    g_dispatcher_table[g_dispatcher_count].cmd = cmd;
    g_dispatcher_table[g_dispatcher_count].handler = handler;
    g_dispatcher_count++;
    return 0;
}

static bool in_handler = false;

int dispatcher_call(uint16_t cmd)
{
    if (in_handler) return -3; // ±‹√‚µ›πÈ

    for (uint8_t i = 0; i < g_dispatcher_count; i++) {
        if (g_dispatcher_table[i].cmd == cmd) {
            if (g_dispatcher_table[i].handler) {
                in_handler = true;
                g_dispatcher_table[i].handler();
                in_handler = false;
            }
            return 0;
        }
    }
    return -1;
}

void dispatcher_print(void)
{
    printf("Dispatcher entries: %d\n", g_dispatcher_count);
    for (uint8_t i = 0; i < g_dispatcher_count; i++) {
        printf("cmd=0x%04X, handler=%p\n", g_dispatcher_table[i].cmd,
               g_dispatcher_table[i].handler);
    }
}