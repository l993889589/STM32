/**
 * @file dwin_tx_policy.c
 * @brief Static priority, coalescing, and retry policy for DWIN output.
 */

#include "dwin_tx_policy.h"

#include <string.h>

/** @brief Return true when a wrap-safe 32-bit deadline has been reached. */
static bool dwin_tx_policy_due(uint32_t now_tick, uint32_t deadline_tick)
{
    return (int32_t)(now_tick - deadline_tick) >= 0;
}

/** @brief Copy a validated event into the tail of the fixed FIFO. */
static bool dwin_tx_policy_push_event(dwin_tx_policy_t *policy,
                                      const dwin_tx_item_t *item)
{
    if(policy->event_count >= DWIN_TX_POLICY_EVENT_CAPACITY)
        return false;

    policy->event_queue[policy->event_tail] = *item;
    policy->event_tail =
        (policy->event_tail + 1U) % DWIN_TX_POLICY_EVENT_CAPACITY;
    policy->event_count++;
    return true;
}

/** @brief Remove the oldest event from the fixed FIFO. */
static bool dwin_tx_policy_pop_event(dwin_tx_policy_t *policy,
                                     dwin_tx_item_t *item)
{
    if(policy->event_count == 0U)
        return false;

    *item = policy->event_queue[policy->event_head];
    policy->event_head =
        (policy->event_head + 1U) % DWIN_TX_POLICY_EVENT_CAPACITY;
    policy->event_count--;
    return true;
}

/** @brief Remove one latest-value slot using round-robin fairness. */
static bool dwin_tx_policy_pop_latest(dwin_tx_policy_t *policy,
                                      dwin_tx_item_t *item)
{
    uint32_t offset;

    for(offset = 0U; offset < DWIN_TX_POLICY_LATEST_CAPACITY; offset++)
    {
        uint32_t index =
            (policy->latest_cursor + offset) %
            DWIN_TX_POLICY_LATEST_CAPACITY;
        dwin_tx_latest_slot_t *slot = &policy->latest_slots[index];

        if(!slot->used)
            continue;

        memset(item, 0, sizeof(*item));
        item->kind = DWIN_TX_ITEM_LATEST;
        item->key = slot->key;
        item->length = slot->length;
        memcpy(item->data, slot->data, slot->length);

        slot->used = false;
        slot->length = 0U;
        policy->latest_cursor =
            (index + 1U) % DWIN_TX_POLICY_LATEST_CAPACITY;
        return true;
    }
    return false;
}

/** @brief Initialize an empty scheduling policy. */
void dwin_tx_policy_init(dwin_tx_policy_t *policy,
                         uint32_t buzzer_period_ticks,
                         uint32_t buzzer_retry_ticks)
{
    if(policy == NULL)
        return;

    memset(policy, 0, sizeof(*policy));
    policy->buzzer_period_ticks =
        buzzer_period_ticks == 0U ? 1U : buzzer_period_ticks;
    policy->buzzer_retry_ticks =
        buzzer_retry_ticks == 0U ? 1U : buzzer_retry_ticks;
}

/** @brief Append one ordered event or reset transaction. */
bool dwin_tx_policy_submit_event(dwin_tx_policy_t *policy,
                                 const dwin_tx_item_t *item)
{
    if(policy == NULL || item == NULL || item->length == 0U ||
       item->length > DWIN_TX_POLICY_MAX_FRAME_BYTES ||
       (item->kind != DWIN_TX_ITEM_EVENT &&
        item->kind != DWIN_TX_ITEM_RESET))
    {
        return false;
    }

    policy->diagnostics.event_submissions++;
    if(!dwin_tx_policy_push_event(policy, item))
    {
        policy->diagnostics.event_rejections++;
        return false;
    }
    return true;
}

/** @brief Store or overwrite the newest dynamic frame for one logical key. */
bool dwin_tx_policy_submit_latest(dwin_tx_policy_t *policy,
                                  uint16_t key,
                                  const uint8_t *data,
                                  uint16_t length)
{
    dwin_tx_latest_slot_t *free_slot = NULL;
    uint32_t index;

    if(policy == NULL || data == NULL || length == 0U ||
       length > DWIN_TX_POLICY_MAX_FRAME_BYTES)
    {
        return false;
    }

    policy->diagnostics.latest_submissions++;
    for(index = 0U; index < DWIN_TX_POLICY_LATEST_CAPACITY; index++)
    {
        dwin_tx_latest_slot_t *slot = &policy->latest_slots[index];

        if(slot->used && slot->key == key)
        {
            slot->length = length;
            memcpy(slot->data, data, length);
            policy->diagnostics.latest_overwrites++;
            return true;
        }
        if(!slot->used && free_slot == NULL)
            free_slot = slot;
    }

    if(free_slot == NULL)
    {
        policy->diagnostics.latest_rejections++;
        return false;
    }

    free_slot->used = true;
    free_slot->key = key;
    free_slot->length = length;
    memcpy(free_slot->data, data, length);
    return true;
}

/** @brief Enable or disable the latched periodic buzzer schedule. */
void dwin_tx_policy_set_buzzer(dwin_tx_policy_t *policy,
                               bool active,
                               uint32_t now_tick)
{
    if(policy == NULL || policy->buzzer_active == active)
        return;

    policy->buzzer_active = active;
    if(active)
    {
        policy->buzzer_due_tick = now_tick;
        policy->diagnostics.buzzer_starts++;
    }
    else
    {
        policy->diagnostics.buzzer_stops++;
    }
}

/** @brief Select and remove the highest-priority ready item. */
bool dwin_tx_policy_take_next(dwin_tx_policy_t *policy,
                              uint32_t now_tick,
                              dwin_tx_item_t *item)
{
    if(policy == NULL || item == NULL)
        return false;

    if(policy->buzzer_active &&
       dwin_tx_policy_due(now_tick, policy->buzzer_due_tick))
    {
        memset(item, 0, sizeof(*item));
        item->kind = DWIN_TX_ITEM_BUZZER;
        item->key = 0x00A0U;
        return true;
    }

    if(policy->retry_pending)
    {
        *item = policy->retry_item;
        policy->retry_pending = false;
        return true;
    }

    if(dwin_tx_policy_pop_event(policy, item))
        return true;

    return dwin_tx_policy_pop_latest(policy, item);
}

/** @brief Feed one result back into retry and buzzer scheduling. */
void dwin_tx_policy_complete(dwin_tx_policy_t *policy,
                             const dwin_tx_item_t *item,
                             bool success,
                             uint32_t now_tick)
{
    if(policy == NULL || item == NULL)
        return;

    if(item->kind == DWIN_TX_ITEM_BUZZER)
    {
        if(!policy->buzzer_active)
            return;

        if(success)
        {
            policy->buzzer_due_tick =
                now_tick + policy->buzzer_period_ticks;
        }
        else
        {
            policy->buzzer_due_tick =
                now_tick + policy->buzzer_retry_ticks;
            policy->diagnostics.buzzer_retries++;
        }
        return;
    }

    if(success)
        return;

    if(item->kind == DWIN_TX_ITEM_LATEST)
    {
        policy->diagnostics.latest_failures++;
        return;
    }

    if(item->retries_remaining > 0U)
    {
        policy->retry_item = *item;
        policy->retry_item.retries_remaining--;
        policy->retry_pending = true;
        policy->diagnostics.event_retries++;
    }
    else
    {
        policy->diagnostics.event_failures++;
    }
}

/** @brief Return the bounded wait until policy work may become ready. */
uint32_t dwin_tx_policy_next_wait(const dwin_tx_policy_t *policy,
                                  uint32_t now_tick,
                                  uint32_t maximum_wait_ticks)
{
    uint32_t buzzer_wait;

    if(policy == NULL)
        return maximum_wait_ticks;
    if(policy->retry_pending || policy->event_count > 0U)
        return 0U;

    for(uint32_t index = 0U;
        index < DWIN_TX_POLICY_LATEST_CAPACITY;
        index++)
    {
        if(policy->latest_slots[index].used)
            return 0U;
    }

    if(!policy->buzzer_active)
        return maximum_wait_ticks;
    if(dwin_tx_policy_due(now_tick, policy->buzzer_due_tick))
        return 0U;

    buzzer_wait = policy->buzzer_due_tick - now_tick;
    return buzzer_wait < maximum_wait_ticks ?
           buzzer_wait : maximum_wait_ticks;
}

/** @brief Copy the current policy diagnostics. */
void dwin_tx_policy_get_diagnostics(
    const dwin_tx_policy_t *policy,
    dwin_tx_policy_diagnostics_t *diagnostics)
{
    if(policy == NULL || diagnostics == NULL)
        return;
    *diagnostics = policy->diagnostics;
}
