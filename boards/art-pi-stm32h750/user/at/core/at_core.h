#ifndef AT_CORE_H
#define AT_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "at_urc.h"

#ifndef AT_LINE_MAX_LEN
#define AT_LINE_MAX_LEN 544U
#endif

#ifndef AT_RESPONSE_MAX_LEN
#define AT_RESPONSE_MAX_LEN 1024U
#endif

typedef enum
{
    AT_RESULT_NONE = 0,
    AT_RESULT_PENDING,
    AT_RESULT_OK,
    AT_RESULT_ERROR,
    AT_RESULT_TIMEOUT,
    AT_RESULT_OVERFLOW
} at_result_t;

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
    uint32_t deadline_ms;
    at_result_t result;
    uint8_t echo_enabled;
} at_client_t;

void at_client_init(at_client_t *at, at_tx_cb_t tx, void *tx_arg);
void at_client_set_line_callback(at_client_t *at, at_line_cb_t cb, void *arg);
int at_client_register_urc(at_client_t *at, const char *prefix, at_urc_handler_t handler, void *arg);
int at_client_send(at_client_t *at, const char *cmd, uint32_t now_ms, uint32_t timeout_ms);
void at_client_input(at_client_t *at, const uint8_t *data, uint32_t len);
at_result_t at_client_poll(at_client_t *at, uint32_t now_ms);
bool at_client_is_busy(const at_client_t *at);
void at_client_clear_result(at_client_t *at);
const char *at_client_response(const at_client_t *at);
uint16_t at_client_response_len(const at_client_t *at);

#endif
