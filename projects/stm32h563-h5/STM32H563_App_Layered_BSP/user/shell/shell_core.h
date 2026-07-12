
#pragma once
#include <stdint.h>
#include "shell_event.h"

typedef struct shell shell_t;

typedef int (*shell_write_fn)(const uint8_t*, uint16_t, void*);

typedef int (*shell_cmd_fn)(shell_t*, int, char**, void*);

typedef struct
{
    const char *name;
    const char *usage;
    const char *summary;
    shell_cmd_fn handler;
    void *arg;
} shell_command_t;

struct shell
{
    shell_write_fn write;
    void *write_arg;

    const shell_command_t *cmds;
    uint16_t cmd_count;

    char line[SHELL_LINE_SIZE];
    uint16_t len;

    shell_event_queue_t evtq;
};

int shell_init(shell_t *s,
               shell_write_fn w, void *arg,
               const shell_command_t *cmds,
               uint16_t n);

void shell_input(shell_t *s, const uint8_t *d, uint16_t len);
void shell_task(shell_t *s);
