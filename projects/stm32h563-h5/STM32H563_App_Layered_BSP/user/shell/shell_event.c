
#include "shell_event.h"

void shell_event_push(shell_event_queue_t *q, shell_event_t *e)
{
    q->q[q->w] = *e;
    q->w = (q->w + 1) % SHELL_EVT_QUEUE_SIZE;
}

int shell_event_pop(shell_event_queue_t *q, shell_event_t *e)
{
    if(q->r == q->w) return -1;
    *e = q->q[q->r];
    q->r = (q->r + 1) % SHELL_EVT_QUEUE_SIZE;
    return 0;
}
