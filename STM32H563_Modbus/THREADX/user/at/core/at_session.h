/*
 * at_session.h
 *
 * Purpose:
 *   Wraps at_core with platform time, sleep, input polling, capture logging,
 *   retry helpers, and length-based binary command reads.
 *
 * Usage:
 *   One session should own one AT UART. Module drivers call the blocking helper
 *   APIs from task context while a poll callback drains the physical transport.
 *   Socket receive commands that return binary payloads must use
 *   at_session_cmd_read_binary() instead of line-delimited response parsing.
 *
 * Constraints:
 *   Fixed-size buffers only. The session does not make the AT UART concurrent:
 *   different socket IDs still share one serialized AT command stream.
 */
#ifndef AT_SESSION_H
#define AT_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "at_core.h"

typedef uint32_t (*at_session_time_cb_t)(void *arg);
typedef void (*at_session_sleep_cb_t)(uint32_t ms, void *arg);
typedef void (*at_session_log_cb_t)(const char *line, void *arg);
typedef void (*at_session_poll_cb_t)(void *arg);

/* Monotonic counters for binary command-read diagnosis. They are intentionally
 * transport-agnostic so a shell or application can expose them without knowing
 * W800, EC20, or another module's command syntax.
 */
typedef struct
{
    uint32_t attempts;
    uint32_t successes;
    uint32_t header_errors;
    uint32_t capacity_errors;
    uint32_t raw_begin_errors;
    uint32_t timeouts;
} at_session_binary_diag_t;

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
    const char *raw_header_prefix;
    uint8_t *raw_buffer;
    uint16_t raw_capacity;
    uint16_t raw_expected;
    at_raw_separator_t raw_separator;
    uint8_t raw_waiting;
    uint8_t raw_done;
    uint8_t raw_error;
    at_session_binary_diag_t binary_diag;
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

/* Send a command, wait for a text header such as "+OK=<len>", then read exactly
 * that many payload bytes through at_core raw mode. The returned buffer contains
 * only network payload bytes; CR/LF inside the payload is preserved.
 */
bool at_session_cmd_read_binary(at_session_t *session,
                                const char *cmd,
                                const char *header_prefix,
                                uint8_t *buffer,
                                uint16_t max_len,
                                uint16_t *out_len,
                                uint32_t timeout_ms,
                                uint8_t retries);

/* Extended binary read used by module drivers whose response has a known
 * separator convention. Use AT_RAW_SEPARATOR_EMPTY_LINE only when the module
 * manual defines an extra blank line between the length header and payload.
 */
bool at_session_cmd_read_binary_ex(at_session_t *session,
                                   const char *cmd,
                                   const char *header_prefix,
                                   uint8_t *buffer,
                                   uint16_t max_len,
                                   uint16_t *out_len,
                                   at_raw_separator_t separator,
                                   uint32_t timeout_ms,
                                   uint8_t retries);
void at_session_binary_diag(const at_session_t *session, at_session_binary_diag_t *diag);
void at_session_binary_diag_reset(at_session_t *session);
bool at_session_raw_is_active(const at_session_t *session);
int at_session_send_raw(at_session_t *session, const uint8_t *data, uint16_t len);

#endif /* AT_SESSION_H */
