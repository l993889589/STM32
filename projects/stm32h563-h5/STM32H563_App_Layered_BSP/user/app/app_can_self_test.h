/**
 * @file app_can_self_test.h
 * @brief Bounded dual-FDCAN cross-link self-test service and diagnostics.
 */

#ifndef APP_CAN_SELF_TEST_H
#define APP_CAN_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    APP_CAN_SELF_TEST_STATE_UNINITIALIZED = 0,
    APP_CAN_SELF_TEST_STATE_INITIALIZING,
    APP_CAN_SELF_TEST_STATE_WAIT_PERIOD,
    APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST,
    APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE,
    APP_CAN_SELF_TEST_STATE_FAULT
} app_can_self_test_state_t;

/** @brief State of one explicitly requested destructive CAN diagnostic. */
typedef enum
{
    APP_CAN_FAULT_STATE_IDLE = 0,
    APP_CAN_FAULT_STATE_REQUESTED,
    APP_CAN_FAULT_STATE_WAIT_BUS_OFF,
    APP_CAN_FAULT_STATE_RECOVERING
} app_can_fault_state_t;

typedef struct
{
    app_can_self_test_state_t state;
    bsp_status_t last_status;
    bool last_cycle_passed;
    uint32_t sequence;
    uint32_t passed_cycles;
    uint32_t failed_cycles;
    uint32_t timeout_count;
    uint32_t ignored_frames;
    uint32_t last_latency_us;
    uint32_t maximum_latency_us;
    uint32_t can1_tx_frames;
    uint32_t can1_rx_frames;
    uint32_t can2_tx_frames;
    uint32_t can2_rx_frames;
    uint32_t can1_error_events;
    uint32_t can2_error_events;
    uint32_t can1_bus_off_events;
    uint32_t can2_bus_off_events;
    uint16_t can1_tx_error_count;
    uint16_t can2_tx_error_count;
    uint16_t can1_rx_error_count;
    uint16_t can2_rx_error_count;
    uint8_t can1_protocol_bus_off;
    uint8_t can2_protocol_bus_off;
    uint32_t recovery_failures;
    app_can_fault_state_t fault_state;
    uint32_t fault_requests;
    uint32_t fault_passes;
    uint32_t fault_failures;
    uint8_t fault_target;
    bool fault_last_passed;
    bsp_status_t fault_last_status;
    uint32_t can1_bitrate_hz;
    uint32_t can2_bitrate_hz;
} app_can_self_test_snapshot_t;

/**
 * @brief Initialize and start both logical FDCAN channels in classic mode.
 * @return BSP status; the service remains in FAULT when initialization fails.
 */
bsp_status_t app_can_self_test_init(void);

/**
 * @brief Advance the bounded request/response state machine.
 * @param now_ms Current wrap-safe monotonic time in milliseconds.
 * @note Call periodically from thread or superloop context, never from an ISR.
 */
void app_can_self_test_step(uint32_t now_ms);

/**
 * @brief Copy one coherent diagnostics snapshot for UI or shell presentation.
 * @param snapshot Destination owned by the caller.
 */
void app_can_self_test_get_snapshot(app_can_self_test_snapshot_t *snapshot);

/**
 * @brief Request one controlled no-ACK bus-off test on CAN1 or CAN2.
 * @param target One-based channel number, either 1 or 2.
 * @return BSP status; busy means another injection is active.
 */
bsp_status_t app_can_self_test_request_bus_off(uint8_t target);

/** @brief Return a stable lower_snake_case fault-injection state name. */
const char *app_can_fault_state_name(app_can_fault_state_t state);

/**
 * @brief Return a stable short name for one self-test state.
 * @param state State to convert.
 * @return Static English text suitable for diagnostics.
 */
const char *app_can_self_test_state_name(app_can_self_test_state_t state);

#endif /* APP_CAN_SELF_TEST_H */
