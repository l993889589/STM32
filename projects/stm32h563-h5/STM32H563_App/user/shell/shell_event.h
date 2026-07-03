
#pragma once
#include <stdint.h>

typedef enum
{
    SHELL_EVT_CMD = 0,
    SHELL_EVT_AT,
    SHELL_EVT_LDC,
    SHELL_EVT_LOG
} shell_event_type_t;

typedef struct
{
    shell_event_type_t type;
    void *payload;
    uint16_t len;
} shell_event_t;

typedef struct
{
    shell_event_t q[SHELL_EVT_QUEUE_SIZE];
    volatile uint16_t w;
    volatile uint16_t r;
} shell_event_queue_t;

void shell_event_push(shell_event_queue_t *q, shell_event_t *e);
int  shell_event_pop(shell_event_queue_t *q, shell_event_t *e);
