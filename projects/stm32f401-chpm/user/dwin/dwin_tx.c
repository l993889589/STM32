/**
 * @file dwin_tx.c
 * @brief Single-owner ThreadX service for serialized DWIN writes and ACKs.
 */

#include "dwin_tx.h"

#include <stddef.h>
#include <string.h>

#include "app_health.h"
#include "bsp_health.h"
#include "bsp_reset.h"
#include "drv_dwin.h"
#include "dwin_protocol.h"
#include "tx_api.h"

#define DWIN_TX_THREAD_PRIORITY       4U
#define DWIN_TX_THREAD_STACK_BYTES    2048U
#define DWIN_TX_WORK_EVENT            (1UL << 0)
#define DWIN_TX_OWNER_MAX_WAIT_MS     20U
#define DWIN_TX_UART_TIMEOUT_MS       40U
#define DWIN_TX_BUZZER_PERIOD_MS      5000U
#define DWIN_TX_BUZZER_RETRY_MS       100U
#define DWIN_TX_RESET_DELAY_MS        20U

/** @brief Internal outcome of one physical DWIN transaction. */
typedef enum
{
    DWIN_TX_RESULT_OK = 0,
    DWIN_TX_RESULT_UART_ERROR
} dwin_tx_result_t;

static TX_THREAD dwin_tx_thread;
static TX_MUTEX dwin_tx_mutex;
static TX_EVENT_FLAGS_GROUP dwin_tx_events;
static uint64_t
    dwin_tx_thread_stack[DWIN_TX_THREAD_STACK_BYTES / sizeof(uint64_t)];
static dwin_tx_policy_t dwin_tx_policy;
static dwin_tx_diagnostics_t dwin_tx_diagnostics;
static uint8_t dwin_tx_buzzer_frame[DWIN_TX_POLICY_MAX_FRAME_BYTES];
static uint16_t dwin_tx_buzzer_frame_length;
static bool dwin_tx_initialized;

/** @brief Convert milliseconds to at least one ThreadX timer tick. */
static ULONG dwin_tx_ms_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks =
        ((uint64_t)milliseconds * TX_TIMER_TICKS_PER_SECOND + 999ULL) /
        1000ULL;

    if(ticks == 0U)
        ticks = 1U;
    return ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (ULONG)ticks;
}

/** @brief Return true when a transport result completed successfully. */
static bool dwin_tx_result_is_success(dwin_tx_result_t result)
{
    return result == DWIN_TX_RESULT_OK;
}

/**
 * @brief Build one CRC-enabled DWIN 0x82 variable write frame.
 * @return Frame length, or zero when the arguments cannot be represented.
 */
static uint16_t dwin_tx_build_write_frame(uint16_t address,
                                          const uint8_t *payload,
                                          uint16_t length,
                                          uint8_t *frame)
{
    uint16_t frame_length = 0U;

    return dwin_protocol_build_write(address,
                                     payload,
                                     length,
                                     frame,
                                     DWIN_TX_POLICY_MAX_FRAME_BYTES,
                                     &frame_length) ?
           frame_length : 0U;
}

/**
 * @brief Execute one serialized bounded UART write.
 * @return Detailed transaction outcome for retry and diagnostics policy.
 */
static dwin_tx_result_t dwin_tx_send_transaction(
    const dwin_tx_item_t *item)
{
    if(item == NULL || item->length == 0U)
        return DWIN_TX_RESULT_UART_ERROR;

    if(drv_dwin_write(item->data,
                      item->length,
                      DWIN_TX_UART_TIMEOUT_MS) != BSP_STATUS_OK)
        return DWIN_TX_RESULT_UART_ERROR;
    return DWIN_TX_RESULT_OK;
}

/** @brief Notify the owner that new work or a buzzer state change exists. */
static void dwin_tx_notify_owner(void)
{
    (void)tx_event_flags_set(&dwin_tx_events,
                             DWIN_TX_WORK_EVENT,
                             TX_OR);
}

/** @brief Copy the prebuilt buzzer frame into a selected buzzer item. */
static void dwin_tx_prepare_buzzer_item(dwin_tx_item_t *item)
{
    item->length = dwin_tx_buzzer_frame_length;
    memcpy(item->data,
           dwin_tx_buzzer_frame,
           dwin_tx_buzzer_frame_length);
}

/** @brief Record one transaction result in the runtime diagnostics. */
static void dwin_tx_record_result(dwin_tx_item_kind_t kind,
                                  dwin_tx_result_t result)
{
    dwin_tx_diagnostics.transactions_started++;
    if(dwin_tx_result_is_success(result))
    {
        dwin_tx_diagnostics.transactions_succeeded++;
        if(kind == DWIN_TX_ITEM_BUZZER)
            dwin_tx_diagnostics.buzzer_successes++;
        return;
    }

    dwin_tx_diagnostics.uart_failures++;
}

/** @brief ThreadX entry that exclusively owns the low-level DWIN writer. */
static void dwin_tx_thread_entry(ULONG input)
{
    (void)input;

    for(;;)
    {
        dwin_tx_item_t item;
        dwin_tx_result_t result;
        uint32_t wait_ticks;
        ULONG actual_flags;
        bool have_item;

        bsp_health_heartbeat(APP_HEALTH_SERVICE_DWIN_TX);
        (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
        have_item = dwin_tx_policy_take_next(
            &dwin_tx_policy,
            (uint32_t)tx_time_get(),
            &item);
        if(have_item && item.kind == DWIN_TX_ITEM_BUZZER)
            dwin_tx_prepare_buzzer_item(&item);
        (void)tx_mutex_put(&dwin_tx_mutex);

        if(have_item)
        {
            result = dwin_tx_send_transaction(&item);

            (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
            dwin_tx_record_result(item.kind, result);
            dwin_tx_policy_complete(&dwin_tx_policy,
                                    &item,
                                    dwin_tx_result_is_success(result),
                                    (uint32_t)tx_time_get());
            (void)tx_mutex_put(&dwin_tx_mutex);

            if(item.reset_after_send &&
               dwin_tx_result_is_success(result))
            {
                tx_thread_sleep(dwin_tx_ms_to_ticks(
                    DWIN_TX_RESET_DELAY_MS));
                bsp_system_reset();
            }
            continue;
        }

        (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
        wait_ticks = dwin_tx_policy_next_wait(
            &dwin_tx_policy,
            (uint32_t)tx_time_get(),
            (uint32_t)dwin_tx_ms_to_ticks(DWIN_TX_OWNER_MAX_WAIT_MS));
        (void)tx_mutex_put(&dwin_tx_mutex);

        (void)tx_event_flags_get(&dwin_tx_events,
                                 DWIN_TX_WORK_EVENT,
                                 TX_OR_CLEAR,
                                 &actual_flags,
                                 (ULONG)wait_ticks);
    }
}

/** @brief Admit one prepared event/reset item under the service mutex. */
static dwin_tx_status_t dwin_tx_submit_prepared_event(
    const dwin_tx_item_t *item)
{
    bool admitted;

    if(!dwin_tx_initialized)
        return DWIN_TX_STATUS_NOT_READY;
    if(item == NULL)
        return DWIN_TX_STATUS_INVALID_ARGUMENT;

    (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
    admitted = dwin_tx_policy_submit_event(&dwin_tx_policy, item);
    if(admitted && item->kind == DWIN_TX_ITEM_RESET)
        dwin_tx_diagnostics.reset_requests++;
    (void)tx_mutex_put(&dwin_tx_mutex);
    if(!admitted)
        return DWIN_TX_STATUS_FULL;

    dwin_tx_notify_owner();
    return DWIN_TX_STATUS_OK;
}

/** @brief Initialize and start the only DWIN transmit owner thread. */
dwin_tx_status_t dwin_tx_init(void)
{
    static const uint8_t buzzer_payload[] = {0x00U, 0x3EU};
    UINT status;

    if(dwin_tx_initialized)
        return DWIN_TX_STATUS_OK;

    dwin_tx_policy_init(
        &dwin_tx_policy,
        (uint32_t)dwin_tx_ms_to_ticks(DWIN_TX_BUZZER_PERIOD_MS),
        (uint32_t)dwin_tx_ms_to_ticks(DWIN_TX_BUZZER_RETRY_MS));
    memset(&dwin_tx_diagnostics, 0, sizeof(dwin_tx_diagnostics));
    dwin_tx_buzzer_frame_length = dwin_tx_build_write_frame(
        0x00A0U,
        buzzer_payload,
        sizeof(buzzer_payload),
        dwin_tx_buzzer_frame);
    if(dwin_tx_buzzer_frame_length == 0U)
        return DWIN_TX_STATUS_INTERNAL_ERROR;

    status = tx_mutex_create(&dwin_tx_mutex, "DWIN TX lock", TX_INHERIT);
    if(status != TX_SUCCESS)
        return DWIN_TX_STATUS_INTERNAL_ERROR;

    status = tx_event_flags_create(&dwin_tx_events, "DWIN TX work");
    if(status != TX_SUCCESS)
    {
        (void)tx_mutex_delete(&dwin_tx_mutex);
        return DWIN_TX_STATUS_INTERNAL_ERROR;
    }

    dwin_tx_initialized = true;
    status = tx_thread_create(&dwin_tx_thread,
                              "DWIN TX owner",
                              dwin_tx_thread_entry,
                              0U,
                              dwin_tx_thread_stack,
                              sizeof(dwin_tx_thread_stack),
                              DWIN_TX_THREAD_PRIORITY,
                              DWIN_TX_THREAD_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status != TX_SUCCESS)
    {
        dwin_tx_initialized = false;
        (void)tx_event_flags_delete(&dwin_tx_events);
        (void)tx_mutex_delete(&dwin_tx_mutex);
        return DWIN_TX_STATUS_INTERNAL_ERROR;
    }
    return DWIN_TX_STATUS_OK;
}

/** @brief Submit an ordered raw DWIN event frame. */
dwin_tx_status_t dwin_tx_submit_raw_event(const uint8_t *frame,
                                          uint16_t length,
                                          uint8_t retry_limit)
{
    dwin_tx_item_t item;

    if(frame == NULL || length == 0U ||
       length > DWIN_TX_POLICY_MAX_FRAME_BYTES)
    {
        return DWIN_TX_STATUS_INVALID_ARGUMENT;
    }

    memset(&item, 0, sizeof(item));
    item.kind = DWIN_TX_ITEM_EVENT;
    item.length = length;
    item.retries_remaining = retry_limit;
    memcpy(item.data, frame, length);
    return dwin_tx_submit_prepared_event(&item);
}

/** @brief Submit one replaceable raw dynamic frame. */
dwin_tx_status_t dwin_tx_submit_raw_latest(uint16_t key,
                                           const uint8_t *frame,
                                           uint16_t length)
{
    bool admitted;

    if(frame == NULL || length == 0U ||
       length > DWIN_TX_POLICY_MAX_FRAME_BYTES)
    {
        return DWIN_TX_STATUS_INVALID_ARGUMENT;
    }
    if(!dwin_tx_initialized)
        return DWIN_TX_STATUS_NOT_READY;

    (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
    admitted = dwin_tx_policy_submit_latest(&dwin_tx_policy,
                                            key,
                                            frame,
                                            length);
    (void)tx_mutex_put(&dwin_tx_mutex);
    if(!admitted)
        return DWIN_TX_STATUS_FULL;

    dwin_tx_notify_owner();
    return DWIN_TX_STATUS_OK;
}

/** @brief Submit the screen-reset frame and reset the MCU after delivery. */
dwin_tx_status_t dwin_tx_submit_reset(const uint8_t *frame,
                                      uint16_t length,
                                      uint8_t retry_limit)
{
    dwin_tx_item_t item;

    if(frame == NULL || length == 0U ||
       length > DWIN_TX_POLICY_MAX_FRAME_BYTES)
    {
        return DWIN_TX_STATUS_INVALID_ARGUMENT;
    }

    memset(&item, 0, sizeof(item));
    item.kind = DWIN_TX_ITEM_RESET;
    item.length = length;
    item.retries_remaining = retry_limit;
    item.reset_after_send = true;
    memcpy(item.data, frame, length);

    return dwin_tx_submit_prepared_event(&item);
}

/** @brief Build and submit an ordered CRC-enabled DWIN 0x82 write. */
dwin_tx_status_t dwin_tx_submit_write_event(uint16_t address,
                                            const uint8_t *payload,
                                            uint16_t length,
                                            uint8_t retry_limit)
{
    uint8_t frame[DWIN_TX_POLICY_MAX_FRAME_BYTES];
    uint16_t frame_length = dwin_tx_build_write_frame(address,
                                                      payload,
                                                      length,
                                                      frame);

    if(frame_length == 0U)
        return DWIN_TX_STATUS_INVALID_ARGUMENT;
    return dwin_tx_submit_raw_event(frame, frame_length, retry_limit);
}

/** @brief Build and submit a latest-value CRC-enabled DWIN 0x82 write. */
dwin_tx_status_t dwin_tx_submit_write_latest(uint16_t address,
                                             const uint8_t *payload,
                                             uint16_t length)
{
    uint8_t frame[DWIN_TX_POLICY_MAX_FRAME_BYTES];
    uint16_t frame_length = dwin_tx_build_write_frame(address,
                                                      payload,
                                                      length,
                                                      frame);

    if(frame_length == 0U)
        return DWIN_TX_STATUS_INVALID_ARGUMENT;
    return dwin_tx_submit_raw_latest(address, frame, frame_length);
}

/** @brief Enable or disable the reliable periodic DWIN buzzer schedule. */
dwin_tx_status_t dwin_tx_set_buzzer(bool active)
{
    if(!dwin_tx_initialized)
        return DWIN_TX_STATUS_NOT_READY;

    (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
    dwin_tx_policy_set_buzzer(&dwin_tx_policy,
                              active,
                              (uint32_t)tx_time_get());
    (void)tx_mutex_put(&dwin_tx_mutex);
    dwin_tx_notify_owner();
    return DWIN_TX_STATUS_OK;
}

/** @brief Copy coherent DWIN transmit diagnostics. */
dwin_tx_status_t dwin_tx_get_diagnostics(
    dwin_tx_diagnostics_t *diagnostics)
{
    if(diagnostics == NULL)
        return DWIN_TX_STATUS_INVALID_ARGUMENT;
    if(!dwin_tx_initialized)
        return DWIN_TX_STATUS_NOT_READY;

    (void)tx_mutex_get(&dwin_tx_mutex, TX_WAIT_FOREVER);
    *diagnostics = dwin_tx_diagnostics;
    dwin_tx_policy_get_diagnostics(&dwin_tx_policy,
                                   &diagnostics->policy);
    (void)tx_mutex_put(&dwin_tx_mutex);
    return DWIN_TX_STATUS_OK;
}
