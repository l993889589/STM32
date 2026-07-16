/**
 * @file dwin_ldc_channel.c
 * @brief Single-owner DWIN byte-stream framing with LDC 2.x and ThreadX.
 */

#include "dwin_ldc_channel.h"

#include <stddef.h>
#include <string.h>

#include "ldc.h"
#include "stm32f4xx.h"

#define DWIN_MAX_FRAME_BYTES       256U
#define DWIN_FRAME_SLOT_COUNT      2U
#define DWIN_IDLE_TIMEOUT_MS       20U
#define DWIN_ACK_MAX_BYTES         8U
#define DWIN_EVENT_PACKET          (1UL << 0)
#define DWIN_EVENT_ACKNOWLEDGEMENT (1UL << 1)
#define DWIN_EVENT_OVERFLOW        (1UL << 2)

static ldc_t dwin_queue = LDC_CONTEXT_INITIALIZER;
static uint8_t dwin_storage[DWIN_MAX_FRAME_BYTES * DWIN_FRAME_SLOT_COUNT];
static ldc_slot_t dwin_slots[DWIN_FRAME_SLOT_COUNT];
static TX_EVENT_FLAGS_GROUP dwin_events;
static TX_MUTEX dwin_request_mutex;
static dwin_ldc_async_handler_t dwin_async_handler;
static dwin_ldc_channel_diagnostics_t dwin_diagnostics;
static uint8_t dwin_acknowledgement[DWIN_ACK_MAX_BYTES];
static uint16_t dwin_acknowledgement_length;
static uint32_t dwin_idle_elapsed_ms;
static bool dwin_frame_open;
static bool dwin_request_active;
static bool dwin_initialized;

/** @brief Save and mask interrupts for ISR/task sharing with LDC. */
static ldc_lock_state_t dwin_irq_lock(void *argument)
{
    ldc_lock_state_t state;

    (void)argument;
    state = __get_PRIMASK();
    __disable_irq();
    return state;
}

/** @brief Restore the exact interrupt mask captured by dwin_irq_lock(). */
static void dwin_irq_unlock(void *argument, ldc_lock_state_t state)
{
    (void)argument;
    __set_PRIMASK(state);
}

/** @brief Return true for the DWIN write acknowledgement variants. */
static bool dwin_is_acknowledgement(const uint8_t *frame, uint16_t length)
{
    static const uint8_t ack_plain[] =
        {0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU};
    static const uint8_t ack_crc[] =
        {0x5AU, 0xA5U, 0x05U, 0x82U, 0x4FU, 0x4BU, 0xA5U, 0xEFU};

    if(frame == NULL)
        return false;
    if(length == sizeof(ack_plain))
        return memcmp(frame, ack_plain, sizeof(ack_plain)) == 0;
    if(length == sizeof(ack_crc))
        return memcmp(frame, ack_crc, sizeof(ack_crc)) == 0;
    return false;
}

/** @brief Publish an acknowledgement to the current serialized requester. */
static void dwin_publish_acknowledgement(const uint8_t *frame, uint16_t length)
{
    ldc_lock_state_t state = dwin_irq_lock(NULL);

    if(dwin_request_active && length <= sizeof(dwin_acknowledgement))
    {
        memcpy(dwin_acknowledgement, frame, length);
        dwin_acknowledgement_length = length;
        dwin_irq_unlock(NULL, state);
        (void)tx_event_flags_set(&dwin_events,
                                 DWIN_EVENT_ACKNOWLEDGEMENT,
                                 TX_OR);
        return;
    }
    dwin_diagnostics.unsolicited_acknowledgements++;
    dwin_irq_unlock(NULL, state);
}

/** @brief Pop and route every currently committed frame from the owner context. */
static void dwin_dispatch_available_frames(void)
{
    uint8_t frame[DWIN_MAX_FRAME_BYTES];
    size_t length;
    ldc_status_t status;

    for(;;)
    {
        status = ldc_frame_read(&dwin_queue,
                                frame,
                                sizeof(frame),
                                &length);
        if(status == LDC_STATUS_EMPTY)
            return;
        if(status != LDC_STATUS_OK)
        {
            dwin_diagnostics.overflow_events++;
            (void)tx_event_flags_set(&dwin_events,
                                     DWIN_EVENT_OVERFLOW,
                                     TX_OR);
            return;
        }
        if(dwin_is_acknowledgement(frame, (uint16_t)length))
        {
            dwin_publish_acknowledgement(frame, (uint16_t)length);
        }
        else if(dwin_async_handler != NULL)
        {
            dwin_diagnostics.asynchronous_frames++;
            dwin_async_handler(frame, (int)length);
        }
    }
}

/** @brief Initialize the only CHPM LDC instance and its ThreadX wait objects. */
bool dwin_ldc_channel_init(dwin_ldc_async_handler_t async_handler)
{
    ldc_config_t config;

    if(dwin_initialized)
        return true;

    memset(&config, 0, sizeof(config));
    config.storage = dwin_storage;
    config.storage_size = sizeof(dwin_storage);
    config.slots = dwin_slots;
    config.slot_count = DWIN_FRAME_SLOT_COUNT;
    config.frame_capacity = DWIN_MAX_FRAME_BYTES;
    config.frame_mode = LDC_FRAME_MODE_MANUAL;
    config.full_policy = LDC_FULL_REJECT_NEW;
    config.lock = dwin_irq_lock;
    config.unlock = dwin_irq_unlock;
    if(ldc_init(&dwin_queue, &config) != LDC_STATUS_OK)
        return false;
    if(tx_event_flags_create(&dwin_events, "DWIN LDC events") != TX_SUCCESS)
    {
        (void)ldc_deinit(&dwin_queue);
        return false;
    }
    if(tx_mutex_create(&dwin_request_mutex, "DWIN request", TX_INHERIT) !=
       TX_SUCCESS)
    {
        (void)tx_event_flags_delete(&dwin_events);
        (void)ldc_deinit(&dwin_queue);
        return false;
    }

    dwin_async_handler = async_handler;
    dwin_initialized = true;
    return true;
}

/** @brief Feed one whole DMA segment without ReceiveToIdle partial commits. */
bool dwin_ldc_channel_feed(const uint8_t *data,
                           uint16_t length,
                           bool frame_boundary)
{
    ldc_write_result_t result;

    if(!dwin_initialized || data == NULL || length == 0U)
        return false;
    result = ldc_rx_write(&dwin_queue, data, length);
    if(result.status != LDC_STATUS_OK || result.accepted_bytes != length ||
       result.rejected_bytes != 0U || result.dropped_frames != 0U ||
       result.overflow_frames != 0U)
    {
        dwin_diagnostics.rejected_chunks++;
        dwin_diagnostics.overflow_events++;
        (void)ldc_rx_abort(&dwin_queue);
        dwin_frame_open = false;
        dwin_idle_elapsed_ms = 0U;
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_OVERFLOW, TX_OR);
        return false;
    }

    dwin_frame_open = true;
    dwin_idle_elapsed_ms = 0U;
    if(frame_boundary)
    {
        result = ldc_rx_idle(&dwin_queue);
        dwin_frame_open = false;
        if(result.status != LDC_STATUS_OK || result.committed_frames != 1U)
        {
            dwin_diagnostics.rejected_chunks++;
            dwin_diagnostics.overflow_events++;
            (void)ldc_rx_abort(&dwin_queue);
            (void)tx_event_flags_set(&dwin_events,
                                     DWIN_EVENT_OVERFLOW,
                                     TX_OR);
            return false;
        }
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_PACKET, TX_OR);
    }
    return true;
}

/** @brief Commit the open DWIN frame after 20 ms of application-owned silence. */
void dwin_ldc_channel_tick(uint32_t elapsed_ms)
{
    ldc_write_result_t result;
    ldc_lock_state_t state;
    bool notify_overflow = false;
    bool notify_packet = false;

    if(!dwin_initialized || elapsed_ms == 0U)
        return;
    state = dwin_irq_lock(NULL);
    if(dwin_frame_open)
    {
        if(UINT32_MAX - dwin_idle_elapsed_ms < elapsed_ms)
            dwin_idle_elapsed_ms = UINT32_MAX;
        else
            dwin_idle_elapsed_ms += elapsed_ms;
        if(dwin_idle_elapsed_ms >= DWIN_IDLE_TIMEOUT_MS)
        {
            result = ldc_rx_idle(&dwin_queue);
            dwin_frame_open = false;
            dwin_idle_elapsed_ms = 0U;
            if(result.status == LDC_STATUS_OK &&
               result.committed_frames == 1U)
            {
                notify_packet = true;
            }
            else
            {
                dwin_diagnostics.rejected_chunks++;
                dwin_diagnostics.overflow_events++;
                (void)ldc_rx_abort(&dwin_queue);
                notify_overflow = true;
            }
        }
    }
    dwin_irq_unlock(NULL, state);

    if(notify_packet)
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_PACKET, TX_OR);
    if(notify_overflow)
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_OVERFLOW, TX_OR);
}

/** @brief Discard only the incomplete DWIN frame after a UART error. */
void dwin_ldc_channel_abort(void)
{
    ldc_lock_state_t state;

    if(dwin_initialized)
    {
        state = dwin_irq_lock(NULL);
        (void)ldc_rx_abort(&dwin_queue);
        dwin_frame_open = false;
        dwin_idle_elapsed_ms = 0U;
        dwin_irq_unlock(NULL, state);
    }
}

/** @brief Wait for and dispatch frames from the single owner thread. */
void dwin_ldc_channel_owner_wait(ULONG wait_option)
{
    ULONG actual_flags;

    if(!dwin_initialized)
        return;
    (void)tx_event_flags_get(&dwin_events,
                             DWIN_EVENT_PACKET | DWIN_EVENT_OVERFLOW,
                             TX_OR_CLEAR,
                             &actual_flags,
                             wait_option);
    dwin_dispatch_available_frames();
}

/** @brief Serialize one request that expects a DWIN acknowledgement. */
bool dwin_ldc_channel_request_begin(ULONG wait_option)
{
    ULONG stale_flags;
    ldc_lock_state_t state;

    if(!dwin_initialized ||
       tx_mutex_get(&dwin_request_mutex, wait_option) != TX_SUCCESS)
        return false;
    (void)tx_event_flags_get(&dwin_events,
                             DWIN_EVENT_ACKNOWLEDGEMENT,
                             TX_OR_CLEAR,
                             &stale_flags,
                             TX_NO_WAIT);
    state = dwin_irq_lock(NULL);
    dwin_acknowledgement_length = 0U;
    dwin_request_active = true;
    dwin_irq_unlock(NULL, state);
    return true;
}

/** @brief Wait for the owner to publish the current request acknowledgement. */
int dwin_ldc_channel_request_wait(uint8_t *buffer,
                                  uint16_t capacity,
                                  ULONG wait_option)
{
    ULONG actual_flags;
    uint16_t length;
    ldc_lock_state_t state;

    if(!dwin_initialized || buffer == NULL || capacity == 0U)
        return -1;
    if(tx_event_flags_get(&dwin_events,
                          DWIN_EVENT_ACKNOWLEDGEMENT,
                          TX_OR_CLEAR,
                          &actual_flags,
                          wait_option) != TX_SUCCESS)
        return 0;

    state = dwin_irq_lock(NULL);
    length = dwin_acknowledgement_length;
    if(length == 0U || length > capacity)
    {
        dwin_irq_unlock(NULL, state);
        return -1;
    }
    memcpy(buffer, dwin_acknowledgement, length);
    dwin_irq_unlock(NULL, state);
    return (int)length;
}

/** @brief Release serialized request ownership after success or failure. */
void dwin_ldc_channel_request_end(void)
{
    ldc_lock_state_t state;

    if(!dwin_initialized)
        return;
    state = dwin_irq_lock(NULL);
    dwin_request_active = false;
    dwin_acknowledgement_length = 0U;
    dwin_irq_unlock(NULL, state);
    (void)tx_mutex_put(&dwin_request_mutex);
}

/** @brief Copy coherent LDC and integration diagnostics. */
bool dwin_ldc_channel_get_diagnostics(
    ldc_stats_t *ldc_stats,
    dwin_ldc_channel_diagnostics_t *channel_stats)
{
    ldc_lock_state_t state;

    if(!dwin_initialized || ldc_stats == NULL || channel_stats == NULL)
        return false;
    state = dwin_irq_lock(NULL);
    if(ldc_get_stats(&dwin_queue, ldc_stats) != LDC_STATUS_OK)
    {
        dwin_irq_unlock(NULL, state);
        return false;
    }
    *channel_stats = dwin_diagnostics;
    dwin_irq_unlock(NULL, state);
    return true;
}
