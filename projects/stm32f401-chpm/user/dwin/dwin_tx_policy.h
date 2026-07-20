/**
 * @file dwin_tx_policy.h
 * @brief Static scheduling policy for the CHPM DWIN transmit owner.
 *
 * The policy is independent of ThreadX and STM32 peripherals. It owns the
 * priority rules, event retries, latest-value coalescing, and the latched
 * buzzer deadline used by the runtime DWIN transmit service.
 */

#ifndef DWIN_TX_POLICY_H
#define DWIN_TX_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DWIN_TX_POLICY_MAX_FRAME_BYTES 258U
#define DWIN_TX_POLICY_EVENT_CAPACITY  12U
#define DWIN_TX_POLICY_LATEST_CAPACITY 8U

/** @brief Kind of work selected by the DWIN transmit scheduler. */
typedef enum
{
    DWIN_TX_ITEM_BUZZER = 0,
    DWIN_TX_ITEM_EVENT,
    DWIN_TX_ITEM_LATEST,
    DWIN_TX_ITEM_RESET
} dwin_tx_item_kind_t;

/** @brief One complete on-wire DWIN transaction selected for transmission. */
typedef struct
{
    dwin_tx_item_kind_t kind;
    uint16_t key;
    uint16_t length;
    uint8_t retries_remaining;
    bool reset_after_send;
    uint8_t data[DWIN_TX_POLICY_MAX_FRAME_BYTES];
} dwin_tx_item_t;

/** @brief Policy-level counters that explain coalescing and admission loss. */
typedef struct
{
    uint32_t event_submissions;
    uint32_t event_rejections;
    uint32_t event_retries;
    uint32_t event_failures;
    uint32_t latest_submissions;
    uint32_t latest_overwrites;
    uint32_t latest_rejections;
    uint32_t latest_failures;
    uint32_t buzzer_starts;
    uint32_t buzzer_stops;
    uint32_t buzzer_retries;
} dwin_tx_policy_diagnostics_t;

/** @brief Internal fixed slot used for one coalesced latest-value frame. */
typedef struct
{
    bool used;
    uint16_t key;
    uint16_t length;
    uint8_t data[DWIN_TX_POLICY_MAX_FRAME_BYTES];
} dwin_tx_latest_slot_t;

/** @brief Complete static state of the DWIN scheduling policy. */
typedef struct
{
    dwin_tx_item_t event_queue[DWIN_TX_POLICY_EVENT_CAPACITY];
    dwin_tx_latest_slot_t latest_slots[DWIN_TX_POLICY_LATEST_CAPACITY];
    dwin_tx_item_t retry_item;
    dwin_tx_policy_diagnostics_t diagnostics;
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;
    uint32_t latest_cursor;
    uint32_t buzzer_due_tick;
    uint32_t buzzer_period_ticks;
    uint32_t buzzer_retry_ticks;
    bool retry_pending;
    bool buzzer_active;
} dwin_tx_policy_t;

/**
 * @brief Initialize an empty scheduling policy.
 * @param policy Policy storage owned by the caller.
 * @param buzzer_period_ticks Interval between successful buzzer commands.
 * @param buzzer_retry_ticks Delay before retrying a failed buzzer command.
 */
void dwin_tx_policy_init(dwin_tx_policy_t *policy,
                         uint32_t buzzer_period_ticks,
                         uint32_t buzzer_retry_ticks);

/**
 * @brief Append one ordered event or reset transaction.
 * @param policy Initialized policy.
 * @param item Complete item copied into the static event queue.
 * @return True when admitted; false for invalid input or a full event queue.
 */
bool dwin_tx_policy_submit_event(dwin_tx_policy_t *policy,
                                 const dwin_tx_item_t *item);

/**
 * @brief Store or overwrite the newest dynamic frame for one logical key.
 * @param policy Initialized policy.
 * @param key Stable key, normally the DWIN VP address.
 * @param data Complete on-wire frame copied before return.
 * @param length Frame length in bytes.
 * @return True when stored; false for invalid input or no free latest slot.
 */
bool dwin_tx_policy_submit_latest(dwin_tx_policy_t *policy,
                                  uint16_t key,
                                  const uint8_t *data,
                                  uint16_t length);

/**
 * @brief Enable or disable the latched periodic buzzer schedule.
 * @param policy Initialized policy.
 * @param active True to schedule an immediate buzzer command.
 * @param now_tick Current wrap-safe scheduler tick.
 */
void dwin_tx_policy_set_buzzer(dwin_tx_policy_t *policy,
                               bool active,
                               uint32_t now_tick);

/**
 * @brief Select and remove the highest-priority ready item.
 * @param policy Initialized policy.
 * @param now_tick Current wrap-safe scheduler tick.
 * @param item Destination for the selected transaction.
 * @return True when work was selected.
 */
bool dwin_tx_policy_take_next(dwin_tx_policy_t *policy,
                              uint32_t now_tick,
                              dwin_tx_item_t *item);

/**
 * @brief Feed one transmission result back into retry and buzzer scheduling.
 * @param policy Initialized policy.
 * @param item Item previously returned by dwin_tx_policy_take_next().
 * @param success True only after the required transport/ACK contract passed.
 * @param now_tick Current wrap-safe scheduler tick.
 */
void dwin_tx_policy_complete(dwin_tx_policy_t *policy,
                             const dwin_tx_item_t *item,
                             bool success,
                             uint32_t now_tick);

/**
 * @brief Return the bounded wait until policy work may become ready.
 * @param policy Initialized policy.
 * @param now_tick Current wrap-safe scheduler tick.
 * @param maximum_wait_ticks Caller-imposed upper wait bound.
 * @return Wait in ticks; zero means work is ready now.
 */
uint32_t dwin_tx_policy_next_wait(const dwin_tx_policy_t *policy,
                                  uint32_t now_tick,
                                  uint32_t maximum_wait_ticks);

/**
 * @brief Copy the current policy diagnostics.
 * @param policy Initialized policy.
 * @param diagnostics Destination for a coherent caller-protected snapshot.
 */
void dwin_tx_policy_get_diagnostics(
    const dwin_tx_policy_t *policy,
    dwin_tx_policy_diagnostics_t *diagnostics);

#endif /* DWIN_TX_POLICY_H */
