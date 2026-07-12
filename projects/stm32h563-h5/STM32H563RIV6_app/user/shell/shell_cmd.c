#include "shell_core.h"
#include <stdio.h>

static int cmd_help(shell_t *s, int argc, char **argv, void *arg)
{
    (void)arg;

    shell_printf(s, "commands:\r\n");

    for(int i = 0; i < s->command_count; i++)
    {
        shell_printf(s, "  %-10s %s\r\n",
            s->commands[i].usage,
            s->commands[i].summary);
    }

    return 0;
}

static int cmd_version(shell_t *s, int argc, char **argv, void *arg)
{
    (void)argc; (void)argv; (void)arg;

    shell_printf(s, "FW v1.0.0\r\n");
    return 0;
}

/* µº≥ˆƒ¨»œ√¸¡Ó */
const shell_command_t g_shell_cmds[] =
{
    {"help", "help", "show help", cmd_help, NULL},
    {"version", "version", "fw version", cmd_version, NULL},
};

const uint16_t g_shell_cmds_count =
    sizeof(g_shell_cmds) / sizeof(g_shell_cmds[0]);