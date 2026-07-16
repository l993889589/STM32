/*
 * test_at_resilience.c
 *
 * Host-side regression tests for AT command/URC separation and receive-stream
 * recovery. The tests use deterministic fake time and perform no I/O.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "at_session.h"

typedef enum
{
    TEST_MODE_NONE = 0,
    TEST_MODE_SINGLE_RESPONSE,
    TEST_MODE_TRANSPORT_ERROR,
    TEST_MODE_TX_FAIL,
    TEST_MODE_ERROR_DURING_RECOVERY,
    TEST_MODE_CAPTURE_ERROR_AFTER_START,
    TEST_MODE_FROZEN_TIME
} test_mode_t;

typedef struct
{
    at_session_t *session;
    const uint8_t *response;
    uint32_t response_len;
    uint32_t now_ms;
    uint32_t tx_count;
    uint32_t urc_count;
    uint8_t injected;
    test_mode_t mode;
} test_transport_t;

/** @brief Record one transmitted command in the fake transport. */
static int test_tx(const uint8_t *data, uint16_t len, void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;

    if(data == NULL || len == 0U || transport == NULL)
        return -1;
    transport->tx_count++;
    return transport->mode == TEST_MODE_TX_FAIL ? -1 : 0;
}

/** @brief Return deterministic monotonic milliseconds. */
static uint32_t test_now(void *arg)
{
    const test_transport_t *transport = (const test_transport_t *)arg;
    return transport != NULL ? transport->now_ms : 0U;
}

/** @brief Advance deterministic time without sleeping the host process. */
static void test_sleep(uint32_t ms, void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;
    if(transport != NULL && transport->mode != TEST_MODE_FROZEN_TIME)
        transport->now_ms += ms;
}

/** @brief Count a registered URC without allowing it into command capture. */
static void test_urc(const char *line, void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;

    if(line != NULL && transport != NULL)
        transport->urc_count++;
}

/** @brief Inject scripted receive events from the session poll callback. */
static void test_poll(void *arg)
{
    test_transport_t *transport = (test_transport_t *)arg;

    if(transport == NULL || transport->session == NULL)
        return;

    if(transport->mode == TEST_MODE_SINGLE_RESPONSE &&
       transport->tx_count != 0U && transport->injected == 0U)
    {
        at_session_input(transport->session,
                         transport->response,
                         transport->response_len);
        transport->injected = 1U;
    }
    else if(transport->mode == TEST_MODE_TRANSPORT_ERROR &&
            transport->tx_count != 0U && transport->injected == 0U)
    {
        at_session_transport_error(transport->session);
        transport->injected = 1U;
    }
    else if(transport->mode == TEST_MODE_ERROR_DURING_RECOVERY &&
            transport->session->desynchronized != 0U &&
            transport->injected == 0U)
    {
        at_session_transport_error(transport->session);
        transport->injected = 1U;
    }
    else if(transport->mode == TEST_MODE_CAPTURE_ERROR_AFTER_START &&
            transport->tx_count != 0U && transport->injected == 0U)
    {
        static const uint8_t scan_start[] = "+OK\r\n";
        at_session_input(transport->session, scan_start, sizeof(scan_start) - 1U);
        transport->injected = 1U;
    }
    else if(transport->mode == TEST_MODE_CAPTURE_ERROR_AFTER_START &&
            transport->injected == 1U)
    {
        at_session_transport_error(transport->session);
        transport->injected = 2U;
    }
}

/** @brief Reset one scripted test without reinitializing accumulated diagnostics. */
static void test_prepare(test_transport_t *transport,
                         test_mode_t mode,
                         const uint8_t *response,
                         uint32_t response_len)
{
    transport->mode = mode;
    transport->response = response;
    transport->response_len = response_len;
    transport->tx_count = 0U;
    transport->injected = 0U;
}

/** @brief Model an externally completed peer reset and discard stale input state. */
static void test_transport_reset(test_transport_t *transport)
{
    test_prepare(transport, TEST_MODE_NONE, NULL, 0U);
    (void)at_session_recover_after_transport_reset(transport->session);
}

/** @brief Report one failed boolean condition. */
static int test_expect(const char *name, int condition)
{
    if(condition != 0)
        return 0;
    (void)fprintf(stderr, "FAIL %s\n", name);
    return 1;
}

int main(void)
{
    static const uint8_t substring_response[] = "NOTICE +OK\r\n";
    static const uint8_t urc_response[] = "+EVENT: +OK\r\n";
    static const uint8_t recovered_response[] = "+RECOVERED\r\n";
    at_session_command_diag_t diag;
    at_session_t session;
    test_transport_t transport;
    uint8_t overflow_response[AT_LINE_MAX_LEN + 16U];
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
    (void)at_client_register_urc(&session.client, "+EVENT:", test_urc, &transport);

    test_prepare(&transport,
                 TEST_MODE_SINGLE_RESPONSE,
                 substring_response,
                 sizeof(substring_response) - 1U);
    failures += test_expect("substring is not a line-prefix success",
                            !at_session_cmd_expect(&session, "AT+SUB", "+OK", 5U, 1U));
    test_transport_reset(&transport);

    test_prepare(&transport, TEST_MODE_CAPTURE_ERROR_AFTER_START, NULL, 0U);
    failures += test_expect("capture-idle checks transport error before idle success",
                            !at_session_cmd_capture_idle(&session,
                                                         "AT+SCAN",
                                                         "+OK",
                                                         1U,
                                                         5U) &&
                            at_session_recovery_required(&session));
    test_transport_reset(&transport);

    test_prepare(&transport, TEST_MODE_FROZEN_TIME, NULL, 0U);
    failures += test_expect("frozen clock exits command wait with recovery latch",
                            !at_session_cmd_expect(&session, "AT+STALL", "+OK", 5U, 1U) &&
                            at_session_recovery_required(&session));
    failures += test_expect("frozen clock cannot authorize recovery without quiet",
                            !at_session_recover_after_transport_reset(&session) &&
                            at_session_recovery_required(&session));
    test_transport_reset(&transport);

    test_prepare(&transport,
                 TEST_MODE_SINGLE_RESPONSE,
                 urc_response,
                 sizeof(urc_response) - 1U);
    failures += test_expect("URC cannot complete a command",
                            !at_session_cmd_expect(&session, "AT+URC", "+EVENT:", 5U, 1U));
    failures += test_expect("URC handler executed", transport.urc_count == 1U);
    failures += test_expect("URC excluded from command capture",
                            strstr(at_session_capture(&session), "+EVENT:") == NULL);
    test_transport_reset(&transport);

    memset(overflow_response, 'X', sizeof(overflow_response));
    memcpy(&overflow_response[sizeof(overflow_response) - 7U], "+OK\r\n", 5U);
    overflow_response[sizeof(overflow_response) - 2U] = '\r';
    overflow_response[sizeof(overflow_response) - 1U] = '\n';
    test_prepare(&transport,
                 TEST_MODE_SINGLE_RESPONSE,
                 overflow_response,
                 sizeof(overflow_response));
    failures += test_expect("overflow tail is discarded until LF",
                            !at_session_cmd_expect(&session, "AT+LONG", "+OK", 5U, 1U));
    test_transport_reset(&transport);

    test_prepare(&transport, TEST_MODE_TRANSPORT_ERROR, NULL, 0U);
    failures += test_expect("transport discontinuity fails active command",
                            !at_session_cmd_expect(&session, "AT+LOSS", "+OK", 5U, 1U));

    test_prepare(&transport, TEST_MODE_ERROR_DURING_RECOVERY, NULL, 0U);
    failures += test_expect("new discontinuity prevents recovery latch clearing",
                            !at_session_recover_after_transport_reset(&session) &&
                            at_session_recovery_required(&session));

    test_prepare(&transport,
                 TEST_MODE_SINGLE_RESPONSE,
                 recovered_response,
                 sizeof(recovered_response) - 1U);
    failures += test_expect("unsafe session rejects commands before peer reset",
                            !at_session_cmd_expect(&session,
                                                   "AT+UNSAFE",
                                                   "+RECOVERED",
                                                   5U,
                                                   1U));
    failures += test_expect("blocked command is never transmitted",
                            transport.tx_count == 0U);
    test_transport_reset(&transport);

    test_prepare(&transport, TEST_MODE_TX_FAIL, NULL, 0U);
    failures += test_expect("raw TX failure latches peer-reset requirement",
                            at_session_send_raw(&session,
                                                (const uint8_t *)"X",
                                                1U) != 0 &&
                            at_session_recovery_required(&session));
    test_transport_reset(&transport);

    test_prepare(&transport, TEST_MODE_TX_FAIL, NULL, 0U);
    failures += test_expect("capture-idle TX failure latches peer-reset requirement",
                            !at_session_cmd_capture_idle(&session,
                                                         "AT+SCAN",
                                                         "+OK",
                                                         1U,
                                                         5U) &&
                            at_session_recovery_required(&session));
    test_transport_reset(&transport);

    test_prepare(&transport,
                 TEST_MODE_SINGLE_RESPONSE,
                 recovered_response,
                 sizeof(recovered_response) - 1U);
    failures += test_expect("command succeeds after quiet resynchronization",
                            at_session_cmd_expect(&session,
                                                  "AT+RECOVER",
                                                  "+RECOVERED",
                                                  5U,
                                                  1U));

    test_prepare(&transport, TEST_MODE_NONE, NULL, 0U);
    failures += test_expect("generic +OK timeout latches peer-reset requirement",
                            !at_session_cmd_expect(&session, "AT+OLD", "+OK", 5U, 1U) &&
                            at_session_recovery_required(&session));
    test_prepare(&transport, TEST_MODE_SINGLE_RESPONSE,
                 (const uint8_t *)"+OK\r\n", 5U);
    failures += test_expect("late same-prefix response cannot complete next command",
                            !at_session_cmd_expect(&session, "AT+NEW", "+OK", 5U, 1U) &&
                            transport.tx_count == 0U && transport.injected == 0U);
    test_transport_reset(&transport);
    test_prepare(&transport, TEST_MODE_SINGLE_RESPONSE,
                 (const uint8_t *)"+OK\r\n", 5U);
    failures += test_expect("same-prefix command succeeds only after explicit peer reset",
                            at_session_cmd_expect(&session, "AT+NEW", "+OK", 5U, 1U));

    at_session_command_diag(&session, &diag);
    failures += test_expect("typed diagnostics recorded",
                            diag.overflows >= 1U &&
                            diag.transport_errors >= 1U &&
                            diag.recovery_blocks >= 2U &&
                            diag.transport_resets >= 5U);

    if(failures != 0)
        return 1;

    (void)printf("PASS at_core resilience: 16 scenarios\n");
    return 0;
}
