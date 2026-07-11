#ifndef AT_COMMAND_PLAN_H
#define AT_COMMAND_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at_session.h"

typedef enum
{
    AT_COMMAND_REQUIRED = 0,
    AT_COMMAND_OPTIONAL
} at_command_requirement_t;

typedef struct
{
    const char *name;
    const char *command;
    const char *expect;
    uint32_t timeout_ms;
    uint8_t retries;
    at_command_requirement_t requirement;
} at_command_step_t;

bool at_command_plan_run(at_session_t *session,
                         const at_command_step_t *steps,
                         size_t step_count,
                         size_t *failed_step);

#endif
