/*
 * test_at_binary.c
 *
 * Purpose:
 *   Host-side regression tests for AT session exact-length binary reads.
 *   The cases protect leading CR/LF payload bytes, W800's extra empty-line
 *   separator, malformed lengths, capacity rejection, timeout cleanup, and
 *   recovery of the next command after an incomplete payload.
 *
 * Usage:
 *   Run tools/test_at_core_host.ps1. The test links only the platform-neutral
 *   AT core/session sources and does not require STM32 HAL or an RTOS.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "at_session.h"

typedef struct
{
    at_session_t *session;
    const uint8_t *response;
    uint32_t response_len;
    uint32_t response_pos;
    uint32_t now_ms;
    uint32_t tx_count;
} test_transport_t;

static int test_tx(const uint8_t *data, uint16_t len, void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;

    if(data == NULL || len == 0U || transport == NULL)
        return -1;
    transport->tx_count++;
    return 0;
}

static uint32_t test_now(void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;
    return transport != NULL ? transport->now_ms : 0U;
}

static void test_sleep(uint32_t ms, void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;
    if(transport != NULL)
        transport->now_ms += ms;
}

static void test_poll(void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;
    uint32_t chunk;

    if(transport == NULL || transport->session == NULL || transport->tx_count == 0U ||
       transport->response_pos >= transport->response_len)
        return;

    chunk = transport->response_len - transport->response_pos;
    if(chunk > 3U)
        chunk = 3U;
    at_session_input(transport->session,
                     &transport->response[transport->response_pos],
                     chunk);
    transport->response_pos += chunk;
}

static void test_prepare(test_transport_t *transport,
                         const uint8_t *response,
                         uint32_t response_len)
{
    transport->response = response;
    transport->response_len = response_len;
    transport->response_pos = 0U;
    transport->tx_count = 0U;
}

static int expect_bytes(const char *name,
                        const uint8_t *actual,
                        const uint8_t *expected,
                        uint16_t len)
{
    if(memcmp(actual, expected, len) == 0)
        return 0;

    (void)fprintf(stderr, "FAIL %s: payload mismatch\n", name);
    return 1;
}

int main(void)
{
    static const uint8_t no_separator_response[] = "+QIRD: 4\r\n\r\nAB";
    static const uint8_t w800_response[] = "+OK=4\r\n\r\n\r\nAB";
    static const uint8_t malformed_response[] = "+OK=oops\r\n";
    static const uint8_t oversized_response[] = "+OK=5\r\n";
    static const uint8_t partial_response[] = "+OK=4\r\n\r\nA";
    static const uint8_t recovery_response[] = "+OK=2\r\n\r\nOK";
    static const uint8_t expected_crlfab[] = {0x0DU, 0x0AU, 'A', 'B'};
    at_session_binary_diag_t diag;
    at_session_t session;
    test_transport_t transport;
    uint8_t output[8];
    uint16_t output_len;
    int failures = 0;

    memset(&transport, 0, sizeof(transport));
    at_session_init(&session,
                    test_tx,
                    &transport,
                    test_now,
                    &transport,
                    test_sleep,
                    &transport);
    transport.session = &session;
    at_session_set_poll_callback(&session, test_poll, &transport);

    memset(output, 0, sizeof(output));
    test_prepare(&transport, no_separator_response, sizeof(no_separator_response) - 1U);
    if(!at_session_cmd_read_binary_ex(&session, "AT+QIRD", "+QIRD: ", output,
                                      sizeof(output), &output_len,
                                      AT_RAW_SEPARATOR_NONE, 50U, 1U) ||
       output_len != 4U)
    {
        (void)fprintf(stderr, "FAIL no-separator: command failed\n");
        failures++;
    }
    else
    {
        failures += expect_bytes("no-separator", output, expected_crlfab, 4U);
    }

    memset(output, 0, sizeof(output));
    test_prepare(&transport, w800_response, sizeof(w800_response) - 1U);
    if(!at_session_cmd_read_binary_ex(&session, "AT+SKRCV", "+OK=", output,
                                      sizeof(output), &output_len,
                                      AT_RAW_SEPARATOR_EMPTY_LINE, 50U, 1U) ||
       output_len != 4U)
    {
        (void)fprintf(stderr, "FAIL W800 separator: command failed\n");
        failures++;
    }
    else
    {
        failures += expect_bytes("W800 separator", output, expected_crlfab, 4U);
    }

    test_prepare(&transport, malformed_response, sizeof(malformed_response) - 1U);
    if(at_session_cmd_read_binary_ex(&session, "AT+BAD", "+OK=", output,
                                     sizeof(output), &output_len,
                                     AT_RAW_SEPARATOR_EMPTY_LINE, 50U, 1U))
    {
        (void)fprintf(stderr, "FAIL malformed length: accepted\n");
        failures++;
    }

    test_prepare(&transport, oversized_response, sizeof(oversized_response) - 1U);
    if(at_session_cmd_read_binary_ex(&session, "AT+BIG", "+OK=", output,
                                     4U, &output_len,
                                     AT_RAW_SEPARATOR_EMPTY_LINE, 50U, 1U))
    {
        (void)fprintf(stderr, "FAIL oversized length: accepted\n");
        failures++;
    }

    test_prepare(&transport, partial_response, sizeof(partial_response) - 1U);
    if(at_session_cmd_read_binary_ex(&session, "AT+PART", "+OK=", output,
                                     sizeof(output), &output_len,
                                     AT_RAW_SEPARATOR_EMPTY_LINE, 20U, 1U))
    {
        (void)fprintf(stderr, "FAIL partial payload: accepted\n");
        failures++;
    }

    memset(output, 0, sizeof(output));
    test_prepare(&transport, recovery_response, sizeof(recovery_response) - 1U);
    if(!at_session_cmd_read_binary_ex(&session, "AT+RECOVER", "+OK=", output,
                                      sizeof(output), &output_len,
                                      AT_RAW_SEPARATOR_EMPTY_LINE, 50U, 1U) ||
       output_len != 2U || memcmp(output, "OK", 2U) != 0)
    {
        (void)fprintf(stderr, "FAIL recovery after timeout\n");
        failures++;
    }

    at_session_binary_diag(&session, &diag);
    if(diag.attempts != 6U || diag.successes != 3U ||
       diag.header_errors != 1U || diag.capacity_errors != 1U ||
       diag.timeouts != 1U)
    {
        (void)fprintf(stderr,
                      "FAIL diagnostics: attempts=%lu success=%lu header=%lu capacity=%lu timeout=%lu\n",
                      (unsigned long)diag.attempts,
                      (unsigned long)diag.successes,
                      (unsigned long)diag.header_errors,
                      (unsigned long)diag.capacity_errors,
                      (unsigned long)diag.timeouts);
        failures++;
    }

    if(failures != 0)
        return 1;

    (void)printf("PASS at_core binary framing: 6 cases\n");
    return 0;
}
