
#include "shell_core.h"
#include <string.h>
#include <stdio.h>

static void shell_exec(shell_t *s, char *line)
{
    char *argv[SHELL_MAX_ARGS];
    int argc = 0;

    while(*line && argc < SHELL_MAX_ARGS)
    {
        while(*line==' ') line++;
        if(!*line) break;

        argv[argc++] = line;

        while(*line && *line!=' ') line++;
        if(*line) *line++ = 0;
    }

    if(argc == 0) return;

    for(int i=0;i<s->cmd_count;i++)
    {
        if(strcmp(argv[0], s->cmds[i].name)==0)
        {
            s->cmds[i].handler(s, argc, argv, s->cmds[i].arg);
            return;
        }
    }

    s->write((const uint8_t*)"cmd not found\r\n",15,s->write_arg);
}

int shell_init(shell_t *s,
               shell_write_fn w, void *arg,
               const shell_command_t *cmds,
               uint16_t n)
{
    memset(s,0,sizeof(*s));
    s->write = w;
    s->write_arg = arg;
    s->cmds = cmds;
    s->cmd_count = n;
    return 0;
}

void shell_input(shell_t *s, const uint8_t *d, uint16_t len)
{
    shell_event_t e = {
        .type = SHELL_EVT_CMD,
        .payload = (void*)d,
        .len = len
    };
    shell_event_push(&s->evtq, &e);
}

void shell_task(shell_t *s)
{
    shell_event_t e;

    while(shell_event_pop(&s->evtq, &e)==0)
    {
        if(e.type == SHELL_EVT_CMD)
        {
            memcpy(s->line, e.payload, e.len);
            s->line[e.len] = 0;
            shell_exec(s, s->line);
        }
    }
}
