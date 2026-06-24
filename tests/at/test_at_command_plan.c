#include "at_command_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int g_failures;
static unsigned int g_calls;
static unsigned int g_fail_call;
static unsigned int g_logs;

#define CHECK(expr)                                                                    \
    do                                                                                 \
    {                                                                                  \
        if(!(expr))                                                                    \
        {                                                                              \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++;                                                              \
        }                                                                              \
    } while(0)

bool at_session_cmd_expect(at_session_t *session,
                           const char *command,
                           const char *expect,
                           uint32_t timeout_ms,
                           uint8_t retries)
{
    (void)session;
    (void)command;
    (void)expect;
    (void)timeout_ms;
    (void)retries;
    g_calls++;
    return g_fail_call == 0U || g_calls != g_fail_call;
}

static void test_log(const char *line, void *arg)
{
    (void)arg;
    CHECK(line != NULL);
    g_logs++;
}

static at_session_t make_session(void)
{
    at_session_t session;
    memset(&session, 0, sizeof(session));
    session.log = test_log;
    return session;
}

static void test_required_and_optional_steps(void)
{
    at_session_t session = make_session();
    const at_command_step_t steps[] =
    {
        {"one", "AT+ONE", "OK", 100U, 1U, AT_COMMAND_REQUIRED},
        {"two", "AT+TWO", "OK", 200U, 2U, AT_COMMAND_OPTIONAL},
        {"three", "AT+THREE", "OK", 300U, 3U, AT_COMMAND_REQUIRED}
    };
    size_t failed;

    g_calls = 0U;
    g_fail_call = 0U;
    g_logs = 0U;
    CHECK(at_command_plan_run(&session, steps, 3U, &failed));
    CHECK(g_calls == 3U);
    CHECK(failed == 3U);
    CHECK(g_logs == 0U);

    g_calls = 0U;
    g_fail_call = 2U;
    g_logs = 0U;
    CHECK(at_command_plan_run(&session, steps, 3U, &failed));
    CHECK(g_calls == 3U);
    CHECK(failed == 1U);
    CHECK(g_logs == 1U);

    g_calls = 0U;
    g_fail_call = 1U;
    g_logs = 0U;
    CHECK(!at_command_plan_run(&session, steps, 3U, &failed));
    CHECK(g_calls == 1U);
    CHECK(failed == 0U);
    CHECK(g_logs == 1U);
}

static void test_invalid_steps_are_rejected(void)
{
    at_session_t session = make_session();
    const at_command_step_t invalid =
        {"invalid", NULL, "OK", 100U, 1U, AT_COMMAND_REQUIRED};
    size_t failed;

    g_calls = 0U;
    g_logs = 0U;
    CHECK(!at_command_plan_run(NULL, &invalid, 1U, &failed));
    CHECK(!at_command_plan_run(&session, &invalid, 1U, &failed));
    CHECK(failed == 0U);
    CHECK(g_calls == 0U);
    CHECK(g_logs == 1U);
}

int main(void)
{
    test_required_and_optional_steps();
    test_invalid_steps_are_rejected();

    if(g_failures != 0U)
        return EXIT_FAILURE;
    (void)printf("AT command plan tests passed\n");
    return EXIT_SUCCESS;
}
