/*
 * at_core.h
 *
 * Purpose:
 *   Defines the lightweight AT byte-stream parser used below at_session and
 *   modem drivers. It owns CR/LF text-line parsing, command result state, URC
 *   dispatch, and the bounded raw receive window used for binary payloads.
 *
 * Usage:
 *   Initialize one at_client_t per AT UART, feed every received byte through
 *   at_client_input(), send commands with at_client_send(), and poll timeouts
 *   with at_client_poll(). Use at_client_raw_begin() only after a higher layer
 *   has parsed a length header for the exact binary payload to follow.
 *
 * Constraints:
 *   No dynamic allocation and no internal locking. The owner of the AT UART
 *   must serialize command sends, raw reads, and response inspection.
 */
#ifndef AT_CORE_H
#define AT_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "at_urc.h"

#ifndef AT_LINE_MAX_LEN
#define AT_LINE_MAX_LEN 544U
#endif

#ifndef AT_RESPONSE_MAX_LEN
#define AT_RESPONSE_MAX_LEN 4096U
#endif

typedef enum
{
    AT_RESULT_NONE = 0,
    AT_RESULT_PENDING,
    AT_RESULT_OK,
    AT_RESULT_ERROR,
    AT_RESULT_TIMEOUT,
    AT_RESULT_OVERFLOW,
    AT_RESULT_TRANSPORT_ERROR
} at_result_t;

/* Selects how many transport separators are consumed after the text header
 * that arms a raw receive window. Most modem commands place payload bytes
 * immediately after the header CR/LF. W800 SKRCV adds one extra empty line.
 */
typedef enum
{
    AT_RAW_SEPARATOR_NONE = 0,
    AT_RAW_SEPARATOR_EMPTY_LINE
} at_raw_separator_t;

typedef int (*at_tx_cb_t)(const uint8_t *data, uint16_t len, void *arg);
typedef void (*at_line_cb_t)(const char *line, void *arg);

typedef struct
{
    at_tx_cb_t tx;
    void *tx_arg;

    at_urc_table_t urc;
    at_line_cb_t line_cb;
    void *line_arg;

    char line_buf[AT_LINE_MAX_LEN];
    uint16_t line_len;

    char response[AT_RESPONSE_MAX_LEN];
    uint16_t response_len;

    const char *active_cmd;
    const char *success_prefix;
    uint32_t deadline_ms;
    uint32_t command_epoch;
    at_result_t result;
    uint8_t echo_enabled;
    uint8_t line_discarding;

    uint8_t *raw_buffer;
    uint16_t raw_expected;
    uint16_t raw_received;
    uint8_t raw_active;
    uint8_t raw_done;
    uint8_t raw_skip_eol;
} at_client_t;

void at_client_init(at_client_t *at, at_tx_cb_t tx, void *tx_arg);
void at_client_set_line_callback(at_client_t *at, at_line_cb_t cb, void *arg);
int at_client_register_urc(at_client_t *at, const char *prefix, at_urc_handler_t handler, void *arg);
int at_client_send(at_client_t *at, const char *cmd, uint32_t now_ms, uint32_t timeout_ms);
int at_client_begin_wait(at_client_t *at,
                         const char *success_prefix,
                         uint32_t now_ms,
                         uint32_t timeout_ms);
void at_client_set_success_prefix(at_client_t *at, const char *success_prefix);
void at_client_transport_error(at_client_t *at);
void at_client_input(at_client_t *at, const uint8_t *data, uint32_t len);
at_result_t at_client_poll(at_client_t *at, uint32_t now_ms);
bool at_client_is_busy(const at_client_t *at);
void at_client_clear_result(at_client_t *at);
const char *at_client_response(const at_client_t *at);
uint16_t at_client_response_len(const at_client_t *at);
bool at_client_raw_begin(at_client_t *at, uint8_t *buffer, uint16_t expected);
bool at_client_raw_begin_ex(at_client_t *at,
                            uint8_t *buffer,
                            uint16_t expected,
                            at_raw_separator_t separator);
void at_client_raw_cancel(at_client_t *at);
bool at_client_raw_is_active(const at_client_t *at);
bool at_client_raw_is_done(const at_client_t *at);
uint16_t at_client_raw_received(const at_client_t *at);

#endif
