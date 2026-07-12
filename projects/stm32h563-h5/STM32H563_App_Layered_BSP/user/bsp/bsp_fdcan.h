/**
 * @file bsp_fdcan.h
 * @brief Logical board FDCAN public interface using physical bit-rate units.
 */

#ifndef BSP_FDCAN_H
#define BSP_FDCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    BOARD_FDCAN_FIELD_1 = 0,
    BOARD_FDCAN_FIELD_2,
    BOARD_FDCAN_COUNT
} board_fdcan_role_t;

typedef struct
{
    uint32_t nominal_bitrate_hz;
    uint16_t nominal_sample_point_permille;
    uint32_t data_bitrate_hz;
    uint16_t data_sample_point_permille;
    bool fd_enabled;
    bool bitrate_switch_enabled;
    bool auto_retransmission;
} bsp_fdcan_config_t;

typedef struct
{
    uint32_t identifier;
    bool extended_id;
    bool remote_frame;
    bool fd_format;
    bool bitrate_switch;
    uint8_t length;
    uint8_t data[64];
} bsp_fdcan_frame_t;

typedef struct
{
    uint32_t achieved_nominal_bitrate_hz;
    uint16_t achieved_nominal_sample_point_permille;
    uint32_t achieved_data_bitrate_hz;
    uint16_t achieved_data_sample_point_permille;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_events;
    uint32_t rx_overruns;
    uint32_t error_events;
    uint32_t warning_events;
    uint32_t passive_events;
    uint32_t bus_off_events;
    uint32_t recovery_attempts;
    uint32_t recovery_failures;
    uint16_t tx_error_count;
    uint16_t rx_error_count;
    uint8_t protocol_error_passive;
    uint8_t protocol_warning;
    uint8_t protocol_bus_off;
    uint8_t last_error_code;
    uint32_t last_driver_error;
} bsp_fdcan_health_t;

/**
 * Initialize and start one logical FDCAN channel.
 * @param role Logical FDCAN role from the board resource manifest.
 * @param config Requested physical bit rates and operating mode.
 * @return BSP status; unsupported timing requests are rejected.
 */
bsp_status_t bsp_fdcan_init(board_fdcan_role_t role,
                            const bsp_fdcan_config_t *config);
/**
 * Stop one initialized logical FDCAN channel.
 * @param role Logical FDCAN role.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_stop(board_fdcan_role_t role);
/**
 * Perform an explicit stop/start recovery after a bus-off or controller fault.
 * @param role Logical FDCAN role.
 * @return BSP status; call from thread or superloop context, never from an ISR.
 */
bsp_status_t bsp_fdcan_recover(board_fdcan_role_t role);
/**
 * Cancel all pending transmit requests for controlled diagnostics.
 * @param role Logical FDCAN role.
 * @return BSP status; call only from the owning service context.
 */
bsp_status_t bsp_fdcan_abort_transmit(board_fdcan_role_t role);
/**
 * Inject one bounded dominant pulse through the selected CAN transceiver.
 * @param role Logical FDCAN role whose TX pin is temporarily overridden.
 * @param pulse_us Pulse width from 1 through 100 microseconds.
 * @return BSP status; intended only for explicit production diagnostics.
 */
bsp_status_t bsp_fdcan_inject_dominant_pulse(board_fdcan_role_t role,
                                             uint32_t pulse_us);
/**
 * Queue one CAN or CAN-FD frame without allocating memory.
 * @param role Logical FDCAN role.
 * @param frame Frame metadata and payload.
 * @return BSP status; busy means the hardware transmit FIFO is full.
 */
bsp_status_t bsp_fdcan_send(board_fdcan_role_t role,
                            const bsp_fdcan_frame_t *frame);
/**
 * Read one frame from the hardware receive FIFO without blocking.
 * @param role Logical FDCAN role.
 * @param frame Receives one frame when available.
 * @param has_frame Receives true when a frame was copied.
 * @return BSP status; an empty FIFO is reported as success with false.
 */
bsp_status_t bsp_fdcan_try_receive(board_fdcan_role_t role,
                                   bsp_fdcan_frame_t *frame,
                                   bool *has_frame);
/**
 * Read a health snapshot for one logical FDCAN channel.
 * @param role Logical FDCAN role.
 * @param health Receives counters and achieved timing.
 * @return BSP status.
 */
bsp_status_t bsp_fdcan_get_health(board_fdcan_role_t role,
                                       bsp_fdcan_health_t *health);

#endif
