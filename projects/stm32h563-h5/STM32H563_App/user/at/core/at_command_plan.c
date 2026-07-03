#include "at_command_plan.h"

#include <stdio.h>

static void at_command_plan_log(at_session_t *session,
                                const char *level,
                                const at_command_step_t *step)
{
    char line[160];

    if(!session || !session->log || !step)
        return;

    (void)snprintf(line,
                   sizeof(line),
                   "at %s: step=%s command=%s",
                   level,
                   step->name ? step->name : "unnamed",
                   step->command ? step->command : "null");
    session->log(line, session->log_arg);
}

bool at_command_plan_run(at_session_t *session,
                         const at_command_step_t *steps,
                         size_t step_count,
                         size_t *failed_step)
{
    if(failed_step)
        *failed_step = step_count;
    if(!session || (!steps && step_count != 0U))
        return false;

    for(size_t i = 0U; i < step_count; i++)
    {
        const at_command_step_t *step = &steps[i];

        if(!step->command || !step->expect || step->retries == 0U)
        {
            if(failed_step)
                *failed_step = i;
            at_command_plan_log(session, "error", step);
            return false;
        }

        if(at_session_cmd_expect(session,
                                 step->command,
                                 step->expect,
                                 step->timeout_ms,
                                 step->retries))
            continue;

        if(failed_step)
            *failed_step = i;
        if(step->requirement == AT_COMMAND_REQUIRED)
        {
            at_command_plan_log(session, "error", step);
            return false;
        }
        at_command_plan_log(session, "warn", step);
    }

    return true;
}
