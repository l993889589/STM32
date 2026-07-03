#include "dispatcher.h"
#include <stdio.h>

static void cmd_version(void)
{
    printf("Version 1.0.0\n");
}

void version_cmd_init(void)
{
    dispatcher_register(0x0001, cmd_version);
}