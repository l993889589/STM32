#include "at_session.h"

#include <stdio.h>
#include <string.h>

static void at_session_capture_append(at_session_t *session, const char *line)
{
    uint32_t len;
    uint32_t room;

    if(!session || !line)
        return;

    len = (uint32_t)strlen(line);
    room = (uint32_t)(sizeof(session->capture) - 1U - session->capture_len);
    if(len > room)
        len = room;

    if(len != 0U)
    {
        memcpy(&session->capture[session->capture_len], line, len);
        session->capture_len = (uint16_t)(session->capture_len + len);
    }

    if(session->capture_len < (sizeof(session->capture) - 1U))
        session->capture[session->capture_len++] = '\n';

    session->capture[session->capture_len] = '\0';
}

static void at_session_line_callback(const char *line, void *arg)
{
    at_session_t *session = (at_session_t *)arg;

    at_session_capture_append(session, line);

    if(session && session->log)
        session->log(line, session->log_arg);
}

uint32_t at_session_now_ms(const at_session_t *session)
{
    if(session && session->now_ms)
        return session->now_ms(session->time_arg);

    return 0U;
}

static void at_session_sleep(at_session_t *session, uint32_t ms)
{
    if(session && session->sleep_ms)
        session->sleep_ms(ms, session->sleep_arg);
}

void at_session_init(at_session_t *session,
                     at_tx_cb_t tx,
                     void *tx_arg,
                     at_session_time_cb_t now_ms,
                     void *time_arg,
                     at_session_sleep_cb_t sleep_ms,
                     void *sleep_arg)
{
    if(!session)
        return;

    memset(session, 0, sizeof(*session));
    at_client_init(&session->client, tx, tx_arg);
    at_client_set_line_callback(&session->client, at_session_line_callback, session);
    session->now_ms = now_ms;
    session->time_arg = time_arg;
    session->sleep_ms = sleep_ms;
    session->sleep_arg = sleep_arg;
}

void at_session_set_logger(at_session_t *session, at_session_log_cb_t log, void *arg)
{
    if(!session)
        return;

    session->log = log;
    session->log_arg = arg;
}

void at_session_set_poll_callback(at_session_t *session, at_session_poll_cb_t poll, void *arg)
{
    if(!session)
        return;

    session->poll = poll;
    session->poll_arg = arg;
}

void at_session_clear_capture(at_session_t *session)
{
    if(!session)
        return;

    session->capture_len = 0U;
    session->capture[0] = '\0';
}

const char *at_session_capture(const at_session_t *session)
{
    return session ? session->capture : "";
}

void at_session_input(at_session_t *session, const uint8_t *data, uint32_t len)
{
    if(!session || !data || len == 0U)
        return;

    at_client_input(&session->client, data, len);
}

void at_session_poll_input(at_session_t *session)
{
    if(session && session->poll)
        session->poll(session->poll_arg);
}

bool at_session_wait_contains(at_session_t *session, const char *token, uint32_t timeout_ms)
{
    const uint32_t start = at_session_now_ms(session);

    if(!session || !token)
        return false;

    while((uint32_t)(at_session_now_ms(session) - start) < timeout_ms)
    {
        at_session_poll_input(session);

        if(strstr(session->capture, token) != NULL ||
           strstr(at_client_response(&session->client), token) != NULL)
        {
            return true;
        }

        at_session_sleep(session, 1U);
    }

    return false;
}

bool at_session_cmd_expect(at_session_t *session,
                           const char *cmd,
                           const char *expect,
                           uint32_t timeout_ms,
                           uint8_t retries)
{
    char line[192];

    if(!session || !cmd || !expect)
        return false;

    for(uint8_t i = 0U; i < retries; i++)
    {
        at_session_clear_capture(session);

        if(session->log)
        {
            (void)snprintf(line, sizeof(line), "at tx: %s", cmd);
            session->log(line, session->log_arg);
        }

        if(at_client_send(&session->client, cmd, at_session_now_ms(session), timeout_ms) == 0)
        {
            while(at_client_is_busy(&session->client))
            {
                at_session_poll_input(session);

                if(strstr(session->capture, expect) != NULL ||
                   strstr(at_client_response(&session->client), expect) != NULL)
                {
                    at_client_clear_result(&session->client);
                    return true;
                }

                if(at_client_poll(&session->client, at_session_now_ms(session)) == AT_RESULT_TIMEOUT)
                    break;

                at_session_sleep(session, 1U);
            }

            if(strstr(session->capture, expect) != NULL ||
               strstr(at_client_response(&session->client), expect) != NULL)
            {
                at_client_clear_result(&session->client);
                return true;
            }
        }

        at_client_clear_result(&session->client);

        if(session->log)
            session->log("at warn: command timeout", session->log_arg);

        at_session_sleep(session, 200U);
    }

    return false;
}

int at_session_send_raw(at_session_t *session, const uint8_t *data, uint16_t len)
{
    if(!session || !session->client.tx || !data || len == 0U)
        return -1;

    return session->client.tx(data, len, session->client.tx_arg);
}
