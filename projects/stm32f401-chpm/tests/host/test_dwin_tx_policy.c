/**
 * @file test_dwin_tx_policy.c
 * @brief Host tests for DWIN priority, coalescing, and retry semantics.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dwin_tx_policy.h"

/** @brief Create one small ordered event used by policy tests. */
static dwin_tx_item_t make_event(uint8_t marker, uint8_t retries)
{
    dwin_tx_item_t item;

    memset(&item, 0, sizeof(item));
    item.kind = DWIN_TX_ITEM_EVENT;
    item.length = 1U;
    item.data[0] = marker;
    item.retries_remaining = retries;
    return item;
}

/** @brief Verify buzzer priority, retry, period, and explicit cancellation. */
static void test_buzzer_is_latched_and_reliable(void)
{
    dwin_tx_policy_t policy;
    dwin_tx_item_t event = make_event(0x11U, 0U);
    dwin_tx_item_t item;

    dwin_tx_policy_init(&policy, 5000U, 100U);
    assert(dwin_tx_policy_submit_event(&policy, &event));
    dwin_tx_policy_set_buzzer(&policy, true, 10U);

    assert(dwin_tx_policy_take_next(&policy, 10U, &item));
    assert(item.kind == DWIN_TX_ITEM_BUZZER);
    dwin_tx_policy_complete(&policy, &item, false, 20U);

    /* Other ordered work may use the retry delay without cancelling it. */
    assert(dwin_tx_policy_take_next(&policy, 20U, &item));
    assert(item.kind == DWIN_TX_ITEM_EVENT);
    assert(item.data[0] == 0x11U);
    assert(dwin_tx_policy_next_wait(&policy, 20U, 1000U) == 100U);

    assert(dwin_tx_policy_take_next(&policy, 120U, &item));
    assert(item.kind == DWIN_TX_ITEM_BUZZER);
    dwin_tx_policy_complete(&policy, &item, true, 130U);

    assert(dwin_tx_policy_next_wait(&policy, 130U, 10000U) == 5000U);

    dwin_tx_policy_set_buzzer(&policy, false, 200U);
    assert(!dwin_tx_policy_take_next(&policy, 6000U, &item));
    assert(policy.diagnostics.buzzer_retries == 1U);
}

/** @brief Verify the newest dynamic value replaces a stale pending value. */
static void test_latest_value_overwrites_by_key(void)
{
    static const uint8_t stale[] = {0x01U, 0x02U};
    static const uint8_t newest[] = {0x03U, 0x04U};
    dwin_tx_policy_t policy;
    dwin_tx_item_t item;

    dwin_tx_policy_init(&policy, 5000U, 100U);
    assert(dwin_tx_policy_submit_latest(&policy,
                                        0x1160U,
                                        stale,
                                        sizeof(stale)));
    assert(dwin_tx_policy_submit_latest(&policy,
                                        0x1160U,
                                        newest,
                                        sizeof(newest)));
    assert(dwin_tx_policy_take_next(&policy, 0U, &item));
    assert(item.kind == DWIN_TX_ITEM_LATEST);
    assert(item.key == 0x1160U);
    assert(item.length == sizeof(newest));
    assert(memcmp(item.data, newest, sizeof(newest)) == 0);

    /* Dynamic delivery failure is counted but never creates stale backlog. */
    dwin_tx_policy_complete(&policy, &item, false, 0U);
    assert(!dwin_tx_policy_take_next(&policy, 0U, &item));
    assert(policy.diagnostics.latest_overwrites == 1U);
    assert(policy.diagnostics.latest_failures == 1U);
}

/** @brief Verify event FIFO order and bounded event retry precedence. */
static void test_events_are_ordered_and_retried(void)
{
    dwin_tx_policy_t policy;
    dwin_tx_item_t first = make_event(0x21U, 1U);
    dwin_tx_item_t second = make_event(0x22U, 0U);
    dwin_tx_item_t item;

    dwin_tx_policy_init(&policy, 5000U, 100U);
    assert(dwin_tx_policy_submit_event(&policy, &first));
    assert(dwin_tx_policy_submit_event(&policy, &second));

    assert(dwin_tx_policy_take_next(&policy, 0U, &item));
    assert(item.data[0] == 0x21U);
    dwin_tx_policy_complete(&policy, &item, false, 0U);

    assert(dwin_tx_policy_take_next(&policy, 0U, &item));
    assert(item.data[0] == 0x21U);
    assert(item.retries_remaining == 0U);
    dwin_tx_policy_complete(&policy, &item, true, 0U);

    assert(dwin_tx_policy_take_next(&policy, 0U, &item));
    assert(item.data[0] == 0x22U);
    assert(policy.diagnostics.event_retries == 1U);
}

/** @brief Verify a fixed event queue fails explicitly instead of overwriting. */
static void test_event_queue_reports_full(void)
{
    dwin_tx_policy_t policy;
    dwin_tx_item_t item = make_event(0x31U, 0U);
    uint32_t index;

    dwin_tx_policy_init(&policy, 5000U, 100U);
    for(index = 0U; index < DWIN_TX_POLICY_EVENT_CAPACITY; index++)
        assert(dwin_tx_policy_submit_event(&policy, &item));
    assert(!dwin_tx_policy_submit_event(&policy, &item));
    assert(policy.diagnostics.event_rejections == 1U);
}

/** @brief Run every host-side DWIN scheduling test. */
int main(void)
{
    test_buzzer_is_latched_and_reliable();
    test_latest_value_overwrites_by_key();
    test_events_are_ordered_and_retried();
    test_event_queue_reports_full();
    puts("DWIN TX policy tests passed");
    return 0;
}
