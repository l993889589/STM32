#include "at_core.h"

#include <string.h>

static uint32_t at_time_after_or_equal(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static int at_is_final_ok(const char *line)
{
    return strcmp(line, "OK") == 0;
}

static int at_is_final_error(const char *line)
{
    return strcmp(line, "ERROR") == 0 ||
           strncmp(line, "+CME ERROR:", 11U) == 0 ||
           strncmp(line, "+CMS ERROR:", 11U) == 0;
}

static void at_append_response(at_client_t *at, const char *line)
{
    size_t line_len = strlen(line);

    if(at->response_len + line_len + 2U >= AT_RESPONSE_MAX_LEN)
    {
        at->result = AT_RESULT_OVERFLOW;
        return;
    }

    memcpy(&at->response[at->response_len], line, line_len);
    at->response_len = (uint16_t)(at->response_len + line_len);
    at->response[at->response_len++] = '\n';
    at->response[at->response_len] = '\0';
}

static void at_handle_line(at_client_t *at, char *line)
{
    if(line[0] == '\0')
        return;

    if(at->line_cb)
        at->line_cb(line, at->line_arg);

    /* URCs are routed before command response collection. */
    if(at_urc_dispatch(&at->urc, line))
        return;

    if(at->result != AT_RESULT_PENDING)
        return;

    if(at->echo_enabled && at->active_cmd && strcmp(line, at->active_cmd) == 0)
        return;

    at_append_response(at, line);

    if(at_is_final_ok(line))
        at->result = AT_RESULT_OK;
    else if(at_is_final_error(line))
        at->result = AT_RESULT_ERROR;
}

void at_client_init(at_client_t *at, at_tx_cb_t tx, void *tx_arg)
{
    if(!at)
        return;

    memset(at, 0, sizeof(*at));
    at->tx = tx;
    at->tx_arg = tx_arg;
    at->result = AT_RESULT_NONE;
    at->echo_enabled = 1U;
    at_urc_init(&at->urc);
}

void at_client_set_line_callback(at_client_t *at, at_line_cb_t cb, void *arg)
{
    if(!at)
        return;

    at->line_cb = cb;
    at->line_arg = arg;
}

int at_client_register_urc(at_client_t *at, const char *prefix, at_urc_handler_t handler, void *arg)
{
    if(!at)
        return -1;

    return at_urc_register(&at->urc, prefix, handler, arg);
}

int at_client_send(at_client_t *at, const char *cmd, uint32_t now_ms, uint32_t timeout_ms)
{
    char out[AT_LINE_MAX_LEN + 2U];
    size_t cmd_len;

    if(!at || !cmd || !at->tx)
        return -1;

    if(at->result == AT_RESULT_PENDING)
        return -2;

    cmd_len = strlen(cmd);
    if(cmd_len + 2U > sizeof(out))
        return -4;

    at->response_len = 0U;
    at->response[0] = '\0';
    at->active_cmd = cmd;
    at->deadline_ms = now_ms + timeout_ms;
    at->result = AT_RESULT_PENDING;

    memcpy(out, cmd, cmd_len);
    out[cmd_len++] = '\r';
    out[cmd_len++] = '\n';

    if(at->tx((const uint8_t *)out, (uint16_t)cmd_len, at->tx_arg) != 0)
    {
        at->result = AT_RESULT_ERROR;
        return -3;
    }

    return 0;
}

void at_client_input(at_client_t *at, const uint8_t *data, uint32_t len)
{
    if(!at || !data)
        return;

    for(uint32_t i = 0U; i < len; i++)
    {
        char ch = (char)data[i];

        if(ch == '\r')
            continue;

        if(ch == '\n')
        {
            at->line_buf[at->line_len] = '\0';
            at_handle_line(at, at->line_buf);
            at->line_len = 0U;
            continue;
        }

        if(at->line_len + 1U >= AT_LINE_MAX_LEN)
        {
            at->line_len = 0U;
            at->result = AT_RESULT_OVERFLOW;
            continue;
        }

        at->line_buf[at->line_len++] = ch;
    }
}

at_result_t at_client_poll(at_client_t *at, uint32_t now_ms)
{
    if(!at)
        return AT_RESULT_ERROR;

    if(at->result == AT_RESULT_PENDING && at_time_after_or_equal(now_ms, at->deadline_ms))
        at->result = AT_RESULT_TIMEOUT;

    return at->result;
}

bool at_client_is_busy(const at_client_t *at)
{
    return at && at->result == AT_RESULT_PENDING;
}

void at_client_clear_result(at_client_t *at)
{
    if(!at)
        return;

    at->result = AT_RESULT_NONE;
    at->active_cmd = NULL;
}

const char *at_client_response(const at_client_t *at)
{
    return at ? at->response : "";
}

uint16_t at_client_response_len(const at_client_t *at)
{
    return at ? at->response_len : 0U;
}
