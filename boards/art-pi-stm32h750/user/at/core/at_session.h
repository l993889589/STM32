#ifndef AT_SESSION_H
#define AT_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "at_core.h"

typedef uint32_t (*at_session_time_cb_t)(void *arg);
typedef void (*at_session_sleep_cb_t)(uint32_t ms, void *arg);
typedef void (*at_session_log_cb_t)(const char *line, void *arg);
typedef void (*at_session_poll_cb_t)(void *arg);

typedef struct
{
    at_client_t client;
    at_session_time_cb_t now_ms;
    at_session_sleep_cb_t sleep_ms;
    at_session_log_cb_t log;
    at_session_poll_cb_t poll;
    void *time_arg;
    void *sleep_arg;
    void *log_arg;
    void *poll_arg;
    char capture[1024];
    uint16_t capture_len;
} at_session_t;

void at_session_init(at_session_t *session,
                     at_tx_cb_t tx,
                     void *tx_arg,
                     at_session_time_cb_t now_ms,
                     void *time_arg,
                     at_session_sleep_cb_t sleep_ms,
                     void *sleep_arg);
void at_session_set_logger(at_session_t *session, at_session_log_cb_t log, void *arg);
void at_session_set_poll_callback(at_session_t *session, at_session_poll_cb_t poll, void *arg);
uint32_t at_session_now_ms(const at_session_t *session);
void at_session_clear_capture(at_session_t *session);
const char *at_session_capture(const at_session_t *session);
void at_session_input(at_session_t *session, const uint8_t *data, uint32_t len);
void at_session_poll_input(at_session_t *session);
bool at_session_wait_contains(at_session_t *session, const char *token, uint32_t timeout_ms);
bool at_session_cmd_expect(at_session_t *session,
                           const char *cmd,
                           const char *expect,
                           uint32_t timeout_ms,
                           uint8_t retries);
int at_session_send_raw(at_session_t *session, const uint8_t *data, uint16_t len);

#endif /* AT_SESSION_H */
