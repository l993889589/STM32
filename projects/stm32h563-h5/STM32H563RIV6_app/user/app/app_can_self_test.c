/**
 * @file app_can_self_test.c
 * @brief Dual-FDCAN physical cross-link request/response self-test service.
 *
 * CAN1 is the cycle initiator. CAN2 only answers a valid request carrying the
 * current sequence, so connecting both transceivers to one short physical bus
 * cannot create an uncontrolled forwarding loop. All waits and FIFO drains are
 * bounded; bus-off recovery is performed from this service context.
 */

#include "app_can_self_test.h"

#include <string.h>

#include "app_blackbox.h"
#include "app_power.h"
#include "bsp_dwt.h"
#include "bsp_fdcan.h"
#include "bsp_irq_lock.h"

#define APP_CAN_SELF_TEST_BITRATE_HZ          500000U
#define APP_CAN_SELF_TEST_SAMPLE_PERMILLE     800U
#define APP_CAN_SELF_TEST_REQUEST_ID          0x561U
#define APP_CAN_SELF_TEST_RESPONSE_ID         0x562U
#define APP_CAN_SELF_TEST_PAYLOAD_LENGTH      8U
#define APP_CAN_SELF_TEST_DIRECTION_REQUEST   1U
#define APP_CAN_SELF_TEST_DIRECTION_RESPONSE  2U
#define APP_CAN_SELF_TEST_RESPONSE_TIMEOUT_MS 100U
#define APP_CAN_SELF_TEST_CYCLE_PERIOD_MS     250U
#define APP_CAN_SELF_TEST_RECOVERY_DELAY_MS   500U
#define APP_CAN_SELF_TEST_MAX_RX_PER_STEP     8U
#define APP_CAN_FAULT_INJECTION_ID             0x56FU
#define APP_CAN_FAULT_TIMEOUT_MS               1500U
#define APP_CAN_FAULT_SOURCE_ID                4U
#define APP_CAN_FAULT_EVENT_BEGIN              1U
#define APP_CAN_FAULT_EVENT_RECOVERED          2U
#define APP_CAN_FAULT_EVENT_FAILED             3U

typedef struct
{
    uint32_t target;
    uint32_t baseline_bus_off;
    uint32_t observed_bus_off;
    int32_t status;
} app_can_fault_record_t;

typedef struct
{
    app_can_self_test_snapshot_t snapshot;
    uint32_t deadline_ms;
    uint32_t cycle_start_us;
    uint32_t last_can1_bus_off_events;
    uint32_t last_can2_bus_off_events;
    uint32_t fault_baseline_bus_off;
    uint32_t fault_deadline_ms;
    uint32_t fault_retry_due_ms;
    uint16_t fault_retry_count;
    bool fault_lock_held;
    bool is_initialized;
} app_can_self_test_context_t;

static app_can_self_test_context_t g_can_self_test;

/** @brief Persist one compact CAN injection transition for later diagnosis. */
static void app_can_fault_record(uint16_t code,
                                 app_blackbox_severity_t severity,
                                 bsp_status_t status)
{
    app_can_fault_record_t payload =
    {
        .target = g_can_self_test.snapshot.fault_target,
        .baseline_bus_off = g_can_self_test.fault_baseline_bus_off,
        .observed_bus_off =
            g_can_self_test.snapshot.fault_target == 1U ?
            g_can_self_test.snapshot.can1_bus_off_events :
            g_can_self_test.snapshot.can2_bus_off_events,
        .status = (int32_t)status
    };

    (void)app_blackbox_record(APP_BLACKBOX_EVENT_FAULT,
                              severity,
                              APP_CAN_FAULT_SOURCE_ID,
                              code,
                              &payload,
                              (uint16_t)sizeof(payload));
}

/** @brief Report whether a wrap-safe millisecond deadline has elapsed. */
static bool app_can_self_test_deadline_elapsed(uint32_t now_ms,
                                               uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/** @brief Store a 32-bit sequence in the fixed diagnostic payload. */
static void app_can_self_test_store_sequence(uint8_t *data, uint32_t sequence)
{
    data[4] = (uint8_t)sequence;
    data[5] = (uint8_t)(sequence >> 8);
    data[6] = (uint8_t)(sequence >> 16);
    data[7] = (uint8_t)(sequence >> 24);
}

/** @brief Read a 32-bit sequence from the fixed diagnostic payload. */
static uint32_t app_can_self_test_load_sequence(const uint8_t *data)
{
    return ((uint32_t)data[4]) |
           ((uint32_t)data[5] << 8) |
           ((uint32_t)data[6] << 16) |
           ((uint32_t)data[7] << 24);
}

/** @brief Build one classic CAN data frame for the current test cycle. */
static void app_can_self_test_build_frame(bsp_fdcan_frame_t *frame,
                                          uint32_t identifier,
                                          uint8_t direction,
                                          uint32_t sequence)
{
    (void)memset(frame, 0, sizeof(*frame));
    frame->identifier = identifier;
    frame->length = APP_CAN_SELF_TEST_PAYLOAD_LENGTH;
    frame->data[0] = 0x4CU;
    frame->data[1] = 0x44U;
    frame->data[2] = direction;
    frame->data[3] = (uint8_t)(~direction);
    app_can_self_test_store_sequence(frame->data, sequence);
}

/** @brief Validate one received frame against the expected cycle token. */
static bool app_can_self_test_frame_matches(const bsp_fdcan_frame_t *frame,
                                            uint32_t identifier,
                                            uint8_t direction,
                                            uint32_t sequence)
{
    return (frame != NULL) &&
           !frame->extended_id &&
           !frame->remote_frame &&
           !frame->fd_format &&
           (frame->identifier == identifier) &&
           (frame->length == APP_CAN_SELF_TEST_PAYLOAD_LENGTH) &&
           (frame->data[0] == 0x4CU) &&
           (frame->data[1] == 0x44U) &&
           (frame->data[2] == direction) &&
           (frame->data[3] == (uint8_t)(~direction)) &&
           (app_can_self_test_load_sequence(frame->data) == sequence);
}

/** @brief Enter the inter-cycle delay after one pass or bounded failure. */
static void app_can_self_test_enter_wait(uint32_t now_ms,
                                         uint32_t delay_ms,
                                         bool cycle_passed)
{
    g_can_self_test.snapshot.last_cycle_passed = cycle_passed;
    g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_WAIT_PERIOD;
    g_can_self_test.deadline_ms = now_ms + delay_ms;
}

/** @brief Record one failed cycle and return to the bounded wait state. */
static void app_can_self_test_fail_cycle(uint32_t now_ms,
                                         bsp_status_t status,
                                         bool timed_out)
{
    g_can_self_test.snapshot.last_status = status;
    ++g_can_self_test.snapshot.failed_cycles;
    if(timed_out)
    {
        ++g_can_self_test.snapshot.timeout_count;
    }
    app_can_self_test_enter_wait(now_ms,
                                 APP_CAN_SELF_TEST_CYCLE_PERIOD_MS,
                                 false);
}

/** @brief Send the next request from CAN1 and arm the CAN2 receive deadline. */
static void app_can_self_test_send_request(uint32_t now_ms)
{
    bsp_fdcan_frame_t request;
    bsp_status_t status;

    ++g_can_self_test.snapshot.sequence;
    app_can_self_test_build_frame(&request,
                                  APP_CAN_SELF_TEST_REQUEST_ID,
                                  APP_CAN_SELF_TEST_DIRECTION_REQUEST,
                                  g_can_self_test.snapshot.sequence);
    status = bsp_fdcan_send(BOARD_FDCAN_FIELD_1, &request);
    if(status != BSP_STATUS_OK)
    {
        app_can_self_test_fail_cycle(now_ms, status, false);
        return;
    }

    g_can_self_test.cycle_start_us = bsp_dwt_get_us();
    g_can_self_test.snapshot.last_status = BSP_STATUS_OK;
    g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST;
    g_can_self_test.deadline_ms = now_ms + APP_CAN_SELF_TEST_RESPONSE_TIMEOUT_MS;
}

/** @brief Answer a valid CAN1 request from CAN2 without forwarding other frames. */
static bsp_status_t app_can_self_test_send_response(void)
{
    bsp_fdcan_frame_t response;

    app_can_self_test_build_frame(&response,
                                  APP_CAN_SELF_TEST_RESPONSE_ID,
                                  APP_CAN_SELF_TEST_DIRECTION_RESPONSE,
                                  g_can_self_test.snapshot.sequence);
    return bsp_fdcan_send(BOARD_FDCAN_FIELD_2, &response);
}

/** @brief Drain a bounded number of CAN2 frames and recognize the request. */
static void app_can_self_test_poll_can2(uint32_t now_ms)
{
    uint32_t count;

    for(count = 0U; count < APP_CAN_SELF_TEST_MAX_RX_PER_STEP; ++count)
    {
        bsp_fdcan_frame_t frame;
        bool has_frame = false;
        bsp_status_t status = bsp_fdcan_try_receive(BOARD_FDCAN_FIELD_2,
                                                    &frame,
                                                    &has_frame);
        if(status != BSP_STATUS_OK)
        {
            app_can_self_test_fail_cycle(now_ms, status, false);
            return;
        }
        if(!has_frame)
        {
            return;
        }
        if((g_can_self_test.snapshot.state ==
            APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST) &&
           app_can_self_test_frame_matches(&frame,
                                           APP_CAN_SELF_TEST_REQUEST_ID,
                                           APP_CAN_SELF_TEST_DIRECTION_REQUEST,
                                           g_can_self_test.snapshot.sequence))
        {
            status = app_can_self_test_send_response();
            if(status != BSP_STATUS_OK)
            {
                app_can_self_test_fail_cycle(now_ms, status, false);
                return;
            }
            g_can_self_test.snapshot.state =
                APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE;
            g_can_self_test.deadline_ms =
                now_ms + APP_CAN_SELF_TEST_RESPONSE_TIMEOUT_MS;
            return;
        }
        ++g_can_self_test.snapshot.ignored_frames;
    }
}

/** @brief Drain a bounded number of CAN1 frames and recognize the response. */
static void app_can_self_test_poll_can1(uint32_t now_ms)
{
    uint32_t count;

    for(count = 0U; count < APP_CAN_SELF_TEST_MAX_RX_PER_STEP; ++count)
    {
        bsp_fdcan_frame_t frame;
        bool has_frame = false;
        bsp_status_t status = bsp_fdcan_try_receive(BOARD_FDCAN_FIELD_1,
                                                    &frame,
                                                    &has_frame);
        if(status != BSP_STATUS_OK)
        {
            app_can_self_test_fail_cycle(now_ms, status, false);
            return;
        }
        if(!has_frame)
        {
            return;
        }
        if((g_can_self_test.snapshot.state ==
            APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE) &&
           app_can_self_test_frame_matches(&frame,
                                           APP_CAN_SELF_TEST_RESPONSE_ID,
                                           APP_CAN_SELF_TEST_DIRECTION_RESPONSE,
                                           g_can_self_test.snapshot.sequence))
        {
            uint32_t latency_us = bsp_dwt_get_us() -
                                  g_can_self_test.cycle_start_us;

            g_can_self_test.snapshot.last_latency_us = latency_us;
            if(latency_us > g_can_self_test.snapshot.maximum_latency_us)
            {
                g_can_self_test.snapshot.maximum_latency_us = latency_us;
            }
            ++g_can_self_test.snapshot.passed_cycles;
            g_can_self_test.snapshot.last_status = BSP_STATUS_OK;
            app_can_self_test_enter_wait(now_ms,
                                         APP_CAN_SELF_TEST_CYCLE_PERIOD_MS,
                                         true);
            return;
        }
        ++g_can_self_test.snapshot.ignored_frames;
    }
}

/** @brief Refresh flattened health counters used by UI and shell clients. */
static bsp_status_t app_can_self_test_refresh_health(void)
{
    bsp_fdcan_health_t can1_health;
    bsp_fdcan_health_t can2_health;
    bsp_status_t status;

    status = bsp_fdcan_get_health(BOARD_FDCAN_FIELD_1, &can1_health);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = bsp_fdcan_get_health(BOARD_FDCAN_FIELD_2, &can2_health);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    g_can_self_test.snapshot.can1_tx_frames = can1_health.tx_frames;
    g_can_self_test.snapshot.can1_rx_frames = can1_health.rx_frames;
    g_can_self_test.snapshot.can2_tx_frames = can2_health.tx_frames;
    g_can_self_test.snapshot.can2_rx_frames = can2_health.rx_frames;
    g_can_self_test.snapshot.can1_error_events = can1_health.error_events;
    g_can_self_test.snapshot.can2_error_events = can2_health.error_events;
    g_can_self_test.snapshot.can1_bus_off_events = can1_health.bus_off_events;
    g_can_self_test.snapshot.can2_bus_off_events = can2_health.bus_off_events;
    g_can_self_test.snapshot.can1_tx_error_count =
        can1_health.tx_error_count;
    g_can_self_test.snapshot.can2_tx_error_count =
        can2_health.tx_error_count;
    g_can_self_test.snapshot.can1_rx_error_count =
        can1_health.rx_error_count;
    g_can_self_test.snapshot.can2_rx_error_count =
        can2_health.rx_error_count;
    g_can_self_test.snapshot.can1_protocol_bus_off =
        can1_health.protocol_bus_off;
    g_can_self_test.snapshot.can2_protocol_bus_off =
        can2_health.protocol_bus_off;
    g_can_self_test.snapshot.recovery_failures =
        can1_health.recovery_failures + can2_health.recovery_failures;
    g_can_self_test.snapshot.can1_bitrate_hz =
        can1_health.achieved_nominal_bitrate_hz;
    g_can_self_test.snapshot.can2_bitrate_hz =
        can2_health.achieved_nominal_bitrate_hz;
    return BSP_STATUS_OK;
}

/** @brief Recover a newly observed bus-off event from service context. */
static bool app_can_self_test_recover_bus_off(uint32_t now_ms)
{
    bsp_status_t status = BSP_STATUS_OK;
    bool recovered = false;

    if(g_can_self_test.snapshot.can1_bus_off_events !=
       g_can_self_test.last_can1_bus_off_events)
    {
        g_can_self_test.last_can1_bus_off_events =
            g_can_self_test.snapshot.can1_bus_off_events;
        status = bsp_fdcan_recover(BOARD_FDCAN_FIELD_1);
        recovered = true;
    }
    if(g_can_self_test.snapshot.can2_bus_off_events !=
       g_can_self_test.last_can2_bus_off_events)
    {
        g_can_self_test.last_can2_bus_off_events =
            g_can_self_test.snapshot.can2_bus_off_events;
        if(bsp_fdcan_recover(BOARD_FDCAN_FIELD_2) != BSP_STATUS_OK)
        {
            status = BSP_STATUS_IO_ERROR;
        }
        recovered = true;
    }
    if(recovered)
    {
        ++g_can_self_test.snapshot.failed_cycles;
        g_can_self_test.snapshot.last_status = status;
        app_can_self_test_enter_wait(now_ms,
                                     APP_CAN_SELF_TEST_RECOVERY_DELAY_MS,
                                     false);
    }
    return recovered;
}

/** @brief End one fault injection, restore both controllers, and publish result. */
static void app_can_fault_finish(uint32_t now_ms,
                                 bool passed,
                                 bsp_status_t status)
{
    bsp_status_t target_status;
    bsp_status_t peer_status;
    bsp_fdcan_role_t target = g_can_self_test.snapshot.fault_target == 1U ?
                                BOARD_FDCAN_FIELD_1 : BOARD_FDCAN_FIELD_2;
    bsp_fdcan_role_t peer = target == BOARD_FDCAN_FIELD_1 ?
                              BOARD_FDCAN_FIELD_2 : BOARD_FDCAN_FIELD_1;

    g_can_self_test.snapshot.fault_state = APP_CAN_FAULT_STATE_RECOVERING;
    target_status = bsp_fdcan_recover(target);
    peer_status = bsp_fdcan_recover(peer);
    if((target_status != BSP_STATUS_OK) || (peer_status != BSP_STATUS_OK))
    {
        passed = false;
        status = BSP_STATUS_IO_ERROR;
    }
    (void)app_can_self_test_refresh_health();
    g_can_self_test.last_can1_bus_off_events =
        g_can_self_test.snapshot.can1_bus_off_events;
    g_can_self_test.last_can2_bus_off_events =
        g_can_self_test.snapshot.can2_bus_off_events;
    g_can_self_test.snapshot.fault_last_passed = passed;
    g_can_self_test.snapshot.fault_last_status = status;
    if(passed)
    {
        ++g_can_self_test.snapshot.fault_passes;
        app_can_fault_record(APP_CAN_FAULT_EVENT_RECOVERED,
                             APP_BLACKBOX_SEVERITY_INFO,
                             status);
    }
    else
    {
        ++g_can_self_test.snapshot.fault_failures;
        app_can_fault_record(APP_CAN_FAULT_EVENT_FAILED,
                             APP_BLACKBOX_SEVERITY_ERROR,
                             status);
    }
    g_can_self_test.snapshot.fault_state = APP_CAN_FAULT_STATE_IDLE;
    app_can_self_test_enter_wait(now_ms,
                                 APP_CAN_SELF_TEST_RECOVERY_DELAY_MS,
                                 false);
    if(g_can_self_test.fault_lock_held)
    {
        app_power_wake_lock_release(APP_POWER_OWNER_CAN);
        g_can_self_test.fault_lock_held = false;
    }
}

/** @brief Advance a controlled peer-isolation bus-off transaction. */
static bool app_can_fault_step(uint32_t now_ms)
{
    bsp_fdcan_role_t target;
    bsp_fdcan_role_t peer;
    uint32_t observed_bus_off;

    if(g_can_self_test.snapshot.fault_state == APP_CAN_FAULT_STATE_IDLE)
    {
        return false;
    }
    if(g_can_self_test.snapshot.fault_state == APP_CAN_FAULT_STATE_REQUESTED)
    {
        bsp_fdcan_frame_t frame;
        bsp_status_t status;

        if(g_can_self_test.snapshot.state != APP_CAN_SELF_TEST_STATE_WAIT_PERIOD)
        {
            return false;
        }
        target = g_can_self_test.snapshot.fault_target == 1U ?
                 BOARD_FDCAN_FIELD_1 : BOARD_FDCAN_FIELD_2;
        peer = target == BOARD_FDCAN_FIELD_1 ?
               BOARD_FDCAN_FIELD_2 : BOARD_FDCAN_FIELD_1;
        g_can_self_test.fault_baseline_bus_off =
            target == BOARD_FDCAN_FIELD_1 ?
            g_can_self_test.snapshot.can1_bus_off_events :
            g_can_self_test.snapshot.can2_bus_off_events;
        status = bsp_fdcan_stop(peer);
        if(status != BSP_STATUS_OK)
        {
            app_can_fault_finish(now_ms, false, status);
            return true;
        }
        app_can_self_test_build_frame(&frame,
                                      APP_CAN_FAULT_INJECTION_ID,
                                      (uint8_t)(0xA0U |
                                      g_can_self_test.snapshot.fault_target),
                                      g_can_self_test.snapshot.fault_requests);
        status = bsp_fdcan_send(target, &frame);
        if(status != BSP_STATUS_OK)
        {
            app_can_fault_finish(now_ms, false, status);
            return true;
        }
        g_can_self_test.fault_deadline_ms =
            now_ms + APP_CAN_FAULT_TIMEOUT_MS;
        g_can_self_test.fault_retry_due_ms = now_ms;
        g_can_self_test.fault_retry_count = 0U;
        g_can_self_test.snapshot.fault_state =
            APP_CAN_FAULT_STATE_WAIT_BUS_OFF;
        app_can_fault_record(APP_CAN_FAULT_EVENT_BEGIN,
                             APP_BLACKBOX_SEVERITY_WARNING,
                             BSP_STATUS_OK);
        return true;
    }

    observed_bus_off = g_can_self_test.snapshot.fault_target == 1U ?
                       g_can_self_test.snapshot.can1_bus_off_events :
                       g_can_self_test.snapshot.can2_bus_off_events;
    if((observed_bus_off > g_can_self_test.fault_baseline_bus_off) ||
       ((g_can_self_test.snapshot.fault_target == 1U) &&
        (g_can_self_test.snapshot.can1_protocol_bus_off != 0U)) ||
       ((g_can_self_test.snapshot.fault_target == 2U) &&
        (g_can_self_test.snapshot.can2_protocol_bus_off != 0U)))
    {
        app_can_fault_finish(now_ms, true, BSP_STATUS_OK);
        return true;
    }
    if(!app_can_self_test_deadline_elapsed(now_ms,
                                           g_can_self_test.fault_retry_due_ms))
    {
        return true;
    }
    target = g_can_self_test.snapshot.fault_target == 1U ?
             BOARD_FDCAN_FIELD_1 : BOARD_FDCAN_FIELD_2;
    if(bsp_fdcan_inject_dominant_pulse(target, 100U) == BSP_STATUS_OK)
    {
        ++g_can_self_test.fault_retry_count;
    }
    g_can_self_test.fault_retry_due_ms = now_ms + 5U;
    if(app_can_self_test_deadline_elapsed(now_ms,
                                          g_can_self_test.fault_deadline_ms))
    {
        app_can_fault_finish(now_ms, false, BSP_STATUS_TIMEOUT);
    }
    return true;
}

/** @brief Initialize the two logical FDCAN roles for cross-link testing. */
bsp_status_t app_can_self_test_init(void)
{
    const bsp_fdcan_config_t config =
    {
        .nominal_bitrate_hz = APP_CAN_SELF_TEST_BITRATE_HZ,
        .nominal_sample_point_permille = APP_CAN_SELF_TEST_SAMPLE_PERMILLE,
        .data_bitrate_hz = APP_CAN_SELF_TEST_BITRATE_HZ,
        .data_sample_point_permille = APP_CAN_SELF_TEST_SAMPLE_PERMILLE,
        .fd_enabled = false,
        .bitrate_switch_enabled = false,
        .auto_retransmission = true
    };
    bsp_status_t status;

    if(g_can_self_test.is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    (void)memset(&g_can_self_test, 0, sizeof(g_can_self_test));
    g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_INITIALIZING;
    status = bsp_fdcan_init(BOARD_FDCAN_FIELD_1, &config);
    if((status != BSP_STATUS_OK) &&
       (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_FAULT;
        g_can_self_test.snapshot.last_status = status;
        return status;
    }
    status = bsp_fdcan_init(BOARD_FDCAN_FIELD_2, &config);
    if((status != BSP_STATUS_OK) &&
       (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_FAULT;
        g_can_self_test.snapshot.last_status = status;
        return status;
    }
    status = app_can_self_test_refresh_health();
    if(status != BSP_STATUS_OK)
    {
        g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_FAULT;
        g_can_self_test.snapshot.last_status = status;
        return status;
    }

    g_can_self_test.is_initialized = true;
    g_can_self_test.snapshot.last_status = BSP_STATUS_OK;
    g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_WAIT_PERIOD;
    g_can_self_test.deadline_ms = 0U;
    return BSP_STATUS_OK;
}

/** @brief Advance one bounded portion of the cross-link self-test. */
void app_can_self_test_step(uint32_t now_ms)
{
    bsp_status_t status;

    if(!g_can_self_test.is_initialized ||
       (g_can_self_test.snapshot.state == APP_CAN_SELF_TEST_STATE_FAULT))
    {
        return;
    }

    status = app_can_self_test_refresh_health();
    if(status != BSP_STATUS_OK)
    {
        g_can_self_test.snapshot.last_status = status;
        g_can_self_test.snapshot.state = APP_CAN_SELF_TEST_STATE_FAULT;
        return;
    }
    if(app_can_fault_step(now_ms))
    {
        return;
    }
    if(app_can_self_test_recover_bus_off(now_ms))
    {
        return;
    }

    app_can_self_test_poll_can2(now_ms);
    app_can_self_test_poll_can1(now_ms);

    if((g_can_self_test.snapshot.state == APP_CAN_SELF_TEST_STATE_WAIT_PERIOD) &&
       app_can_self_test_deadline_elapsed(now_ms,
                                          g_can_self_test.deadline_ms))
    {
        app_can_self_test_send_request(now_ms);
    }
    else if(((g_can_self_test.snapshot.state ==
              APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST) ||
             (g_can_self_test.snapshot.state ==
              APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE)) &&
            app_can_self_test_deadline_elapsed(now_ms,
                                               g_can_self_test.deadline_ms))
    {
        app_can_self_test_fail_cycle(now_ms, BSP_STATUS_TIMEOUT, true);
    }
}

/** @brief Queue one explicit bus-off injection for the CAN owner thread. */
bsp_status_t app_can_self_test_request_bus_off(uint8_t target)
{
    bsp_irq_state_t irq_state;

    if((target != 1U) && (target != 2U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    irq_state = bsp_irq_lock();
    if(!g_can_self_test.is_initialized ||
       (g_can_self_test.snapshot.fault_state != APP_CAN_FAULT_STATE_IDLE))
    {
        bsp_irq_unlock(irq_state);
        return BSP_STATUS_BUSY;
    }
    g_can_self_test.snapshot.fault_target = target;
    g_can_self_test.snapshot.fault_last_passed = false;
    g_can_self_test.snapshot.fault_last_status = BSP_STATUS_BUSY;
    ++g_can_self_test.snapshot.fault_requests;
    g_can_self_test.snapshot.fault_state = APP_CAN_FAULT_STATE_REQUESTED;
    g_can_self_test.fault_lock_held = true;
    app_power_wake_lock_acquire(APP_POWER_OWNER_CAN);
    bsp_irq_unlock(irq_state);
    return BSP_STATUS_OK;
}

/** @brief Copy diagnostics while preventing a scheduler tick from tearing it. */
void app_can_self_test_get_snapshot(app_can_self_test_snapshot_t *snapshot)
{
    bsp_irq_state_t irq_state;

    if(snapshot == NULL)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    *snapshot = g_can_self_test.snapshot;
    bsp_irq_unlock(irq_state);
}

/** @brief Convert one service state to compact stable diagnostic text. */
const char *app_can_self_test_state_name(app_can_self_test_state_t state)
{
    switch(state)
    {
        case APP_CAN_SELF_TEST_STATE_UNINITIALIZED:
            return "OFF";
        case APP_CAN_SELF_TEST_STATE_INITIALIZING:
            return "INIT";
        case APP_CAN_SELF_TEST_STATE_WAIT_PERIOD:
            return "RUN";
        case APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST:
            return "CAN1>CAN2";
        case APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE:
            return "CAN2>CAN1";
        case APP_CAN_SELF_TEST_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

/** @brief Convert one fault-injection state to stable diagnostic text. */
const char *app_can_fault_state_name(app_can_fault_state_t state)
{
    switch(state)
    {
        case APP_CAN_FAULT_STATE_IDLE:         return "idle";
        case APP_CAN_FAULT_STATE_REQUESTED:    return "requested";
        case APP_CAN_FAULT_STATE_WAIT_BUS_OFF: return "wait_bus_off";
        case APP_CAN_FAULT_STATE_RECOVERING:   return "recovering";
        default:                               return "unknown";
    }
}
