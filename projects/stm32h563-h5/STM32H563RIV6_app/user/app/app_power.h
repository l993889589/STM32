/**
 * @file app_power.h
 * @brief Controlled STM32H563 Stop-mode service and persistent wake report.
 */

#ifndef APP_POWER_H
#define APP_POWER_H

#include <stdint.h>

#include "tx_api.h"

typedef enum
{
    APP_POWER_WAKE_SOURCE_RTC = (1U << 0),
    APP_POWER_WAKE_SOURCE_TOUCH = (1U << 1),
    APP_POWER_WAKE_SOURCE_W800 = (1U << 2)
} app_power_wake_source_t;

typedef enum
{
    APP_POWER_STATE_IDLE = 0,
    APP_POWER_STATE_PENDING,
    APP_POWER_STATE_PREPARING,
    APP_POWER_STATE_SLEEPING,
    APP_POWER_STATE_RESTORING,
    APP_POWER_STATE_COMPLETED,
    APP_POWER_STATE_FAILED
} app_power_state_t;

/** @brief Stable owners used by the low-power wake-lock arbiter. */
typedef enum
{
    APP_POWER_OWNER_USB = 0,
    APP_POWER_OWNER_RS485,
    APP_POWER_OWNER_CAN,
    APP_POWER_OWNER_SELF_TEST,
    APP_POWER_OWNER_W800,
    APP_POWER_OWNER_BLACKBOX,
    APP_POWER_OWNER_OTA,
    APP_POWER_OWNER_USER,
    APP_POWER_OWNER_COUNT
} app_power_owner_t;

typedef struct
{
    app_power_state_t state;
    uint32_t generation;
    uint32_t requested_sleep_seconds;
    uint32_t measured_sleep_seconds;
    uint32_t requested_sources;
    uint32_t armed_sources;
    uint32_t wake_reason;
    uint32_t sleep_count;
    uint32_t rejected_count;
    uint32_t restore_error_count;
    uint32_t active_lock_mask;
    uint32_t deadline_mask;
    uint32_t next_deadline_ms;
    uint32_t auto_idle_ms;
    uint32_t auto_max_sleep_seconds;
    uint32_t auto_sleep_count;
    uint32_t auto_blocked_count;
    uint8_t auto_enabled;
    int32_t last_error;
} app_power_snapshot_t;

/** @brief Initialize the controlled power service state. */
void app_power_init(void);

/**
 * @brief Queue one bounded Stop-mode request.
 * @param seconds RTC safety timeout from 1 through 65536 seconds.
 * @param wake_sources Requested RTC, touch, and/or W800 source bits.
 * @return Zero when accepted, otherwise -1.
 */
int app_power_request_stop(uint32_t seconds, uint32_t wake_sources);

/** @brief Acquire one nestable wake lock before a non-interruptible activity. */
void app_power_wake_lock_acquire(app_power_owner_t owner);

/** @brief Release one previously acquired wake lock. */
void app_power_wake_lock_release(app_power_owner_t owner);

/** @brief Publish the owner's next relative deadline, or clear it with zero. */
void app_power_set_deadline(app_power_owner_t owner, uint32_t delay_ms);

/** @brief Reset the automatic-idle timer after externally visible activity. */
void app_power_note_activity(app_power_owner_t owner);

/**
 * @brief Configure automatic Stop arbitration.
 * @return Zero when accepted, otherwise -1 for invalid timing or sources.
 */
int app_power_configure_auto(uint8_t enabled,
                             uint32_t idle_ms,
                             uint32_t max_sleep_seconds,
                             uint32_t wake_sources);

/** @brief Return the stable lower_snake_case name for one wake-lock owner. */
const char *app_power_owner_name(app_power_owner_t owner);

/** @brief Copy one coherent power-service report. */
void app_power_get_snapshot(app_power_snapshot_t *snapshot);

/** @brief Execute queued Stop requests from the dedicated ThreadX owner. */
void app_power_task_entry(ULONG thread_input);

/** @brief Return a stable lower_snake_case execution-state name. */
const char *app_power_state_name(app_power_state_t state);

/** @brief Format a stable wake-source bitset name into caller storage. */
void app_power_format_wake_reason(uint32_t reason, char *text, uint32_t size);

#endif /* APP_POWER_H */
