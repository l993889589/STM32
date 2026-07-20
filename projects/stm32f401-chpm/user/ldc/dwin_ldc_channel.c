/**
 * @file dwin_ldc_channel.c
 * @brief DWIN DMA chunk handoff, length framing, and single-owner dispatch.
 */

#include "dwin_ldc_channel.h"

#include <stddef.h>
#include <string.h>

#include "dwin_protocol.h"
#include "dwin_rx_parser.h"
#include "ldc.h"
#include "stm32f4xx.h"

#define DWIN_FRAME_SLOT_COUNT      2U
#define DWIN_RX_CHUNK_MAX_BYTES    128U
#define DWIN_RX_CHUNK_COUNT        4U
#define DWIN_IDLE_TIMEOUT_MS       20U
#define DWIN_EVENT_RX_WORK         (1UL << 0)
#define DWIN_EVENT_OVERFLOW        (1UL << 2)

/** @brief One bounded DMA publication copied before the HAL buffer is reused. */
typedef struct
{
    uint8_t data[DWIN_RX_CHUNK_MAX_BYTES];
    uint16_t length;
    bool frame_boundary;
} dwin_rx_chunk_t;

static ldc_t dwin_queue = LDC_CONTEXT_INITIALIZER;
static uint8_t
    dwin_storage[DWIN_RX_MAX_FRAME_BYTES * DWIN_FRAME_SLOT_COUNT];
static ldc_slot_t dwin_slots[DWIN_FRAME_SLOT_COUNT];
static dwin_rx_parser_t dwin_parser;
static dwin_rx_chunk_t dwin_rx_chunks[DWIN_RX_CHUNK_COUNT];
static TX_EVENT_FLAGS_GROUP dwin_events;
static dwin_ldc_async_handler_t dwin_async_handler;
static dwin_ldc_channel_diagnostics_t dwin_diagnostics;
static uint8_t dwin_rx_head;
static uint8_t dwin_rx_tail;
static uint8_t dwin_rx_count;
static uint32_t dwin_idle_elapsed_ms;
static uint32_t dwin_activity_generation;
static uint32_t dwin_timeout_generation;
static bool dwin_idle_tracking;
static bool dwin_timeout_pending;
static bool dwin_discontinuity;
static bool dwin_initialized;

/** @brief Save and mask interrupts for ISR/task shared state. */
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

/** @brief Pop and route every committed frame from the owner context. */
static void dwin_dispatch_available_frames(void)
{
    uint8_t frame[DWIN_RX_MAX_FRAME_BYTES];
    size_t length;
    ldc_status_t status;

    for(;;)
    {
        dwin_protocol_ack_t acknowledgement;

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
        acknowledgement =
            dwin_protocol_classify_ack(frame, (uint16_t)length);
        if(acknowledgement == DWIN_PROTOCOL_ACK_PLAIN)
        {
            dwin_diagnostics.acknowledgements_plain++;
            dwin_diagnostics.acknowledgement_activity++;
        }
        else if(acknowledgement == DWIN_PROTOCOL_ACK_CRC)
        {
            dwin_diagnostics.acknowledgements_crc++;
            dwin_diagnostics.acknowledgement_activity++;
        }
        else if(dwin_async_handler != NULL)
        {
            dwin_diagnostics.asynchronous_frames++;
            dwin_async_handler(frame, (int)length);
        }
    }
}

/**
 * @brief Transactionally deliver one exact parser frame through canonical LDC.
 * @return True only when LDC accepted, committed, and dispatched the frame.
 */
static bool dwin_commit_exact_frame(const uint8_t *frame,
                                    uint16_t length,
                                    void *context)
{
    ldc_write_result_t result;

    (void)context;
    result = ldc_rx_write(&dwin_queue, frame, length);
    if(result.status != LDC_STATUS_OK ||
       result.accepted_bytes != length ||
       result.rejected_bytes != 0U ||
       result.dropped_frames != 0U ||
       result.overflow_frames != 0U)
    {
        dwin_diagnostics.rejected_chunks++;
        dwin_diagnostics.overflow_events++;
        (void)ldc_rx_abort(&dwin_queue);
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_OVERFLOW, TX_OR);
        return false;
    }

    result = ldc_rx_idle(&dwin_queue);
    if(result.status != LDC_STATUS_OK || result.committed_frames != 1U)
    {
        dwin_diagnostics.rejected_chunks++;
        dwin_diagnostics.overflow_events++;
        (void)ldc_rx_abort(&dwin_queue);
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_OVERFLOW, TX_OR);
        return false;
    }

    /*
     * Dispatch immediately so the two LDC slots cannot fill while one DMA
     * chunk contains several complete DWIN frames.
     */
    dwin_dispatch_available_frames();
    return true;
}

/** @brief Copy and remove the oldest ISR publication. */
static bool dwin_pop_rx_chunk(dwin_rx_chunk_t *chunk)
{
    ldc_lock_state_t state;

    if(chunk == NULL)
        return false;
    state = dwin_irq_lock(NULL);
    if(dwin_rx_count == 0U)
    {
        dwin_irq_unlock(NULL, state);
        return false;
    }
    *chunk = dwin_rx_chunks[dwin_rx_head];
    dwin_rx_head = (uint8_t)((dwin_rx_head + 1U) % DWIN_RX_CHUNK_COUNT);
    dwin_rx_count--;
    dwin_irq_unlock(NULL, state);
    return true;
}

/** @brief Recover owner state after any lost DMA publication or UART error. */
static bool dwin_recover_discontinuity(void)
{
    ldc_lock_state_t state;
    bool recover;

    state = dwin_irq_lock(NULL);
    recover = dwin_discontinuity;
    if(recover)
    {
        /*
         * Once one byte range is unknown, no queued suffix can be trusted to
         * belong to the parser's current frame. Drop it and resynchronize.
         */
        dwin_discontinuity = false;
        dwin_rx_head = 0U;
        dwin_rx_tail = 0U;
        dwin_rx_count = 0U;
        dwin_timeout_pending = false;
        dwin_idle_tracking = false;
        dwin_idle_elapsed_ms = 0U;
    }
    dwin_irq_unlock(NULL, state);

    if(recover)
    {
        dwin_rx_parser_abort(&dwin_parser);
        (void)ldc_reset(&dwin_queue);
    }
    return recover;
}

/** @brief Apply a still-current software silence marker in owner context. */
static void dwin_apply_idle_timeout(void)
{
    ldc_lock_state_t state;
    uint32_t generation;
    bool pending;

    state = dwin_irq_lock(NULL);
    pending = dwin_timeout_pending;
    generation = dwin_timeout_generation;
    dwin_timeout_pending = false;
    dwin_irq_unlock(NULL, state);

    if(pending && generation == dwin_activity_generation)
        (void)dwin_rx_parser_on_idle(&dwin_parser);
}

/** @brief Initialize the only CHPM LDC instance and its ThreadX wait objects. */
bool dwin_ldc_channel_init(dwin_ldc_async_handler_t async_handler)
{
    ldc_config_t config;

    if(dwin_initialized)
        return true;

    memset(&config, 0, sizeof(config));
    memset(&dwin_diagnostics, 0, sizeof(dwin_diagnostics));
    dwin_rx_parser_init(&dwin_parser);
    config.storage = dwin_storage;
    config.storage_size = sizeof(dwin_storage);
    config.slots = dwin_slots;
    config.slot_count = DWIN_FRAME_SLOT_COUNT;
    config.frame_capacity = DWIN_RX_MAX_FRAME_BYTES;
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

    dwin_async_handler = async_handler;
    dwin_initialized = true;
    return true;
}

/** @brief Queue one DMA segment for parser work in the DWIN owner thread. */
bool dwin_ldc_channel_feed(const uint8_t *data,
                           uint16_t length,
                           bool frame_boundary)
{
    dwin_rx_chunk_t *chunk;
    ldc_lock_state_t state;
    bool accepted = false;

    if(!dwin_initialized || data == NULL || length == 0U ||
       length > DWIN_RX_CHUNK_MAX_BYTES)
    {
        return false;
    }

    state = dwin_irq_lock(NULL);
    if(dwin_rx_count < DWIN_RX_CHUNK_COUNT && !dwin_discontinuity)
    {
        chunk = &dwin_rx_chunks[dwin_rx_tail];
        memcpy(chunk->data, data, length);
        chunk->length = length;
        chunk->frame_boundary = frame_boundary;
        dwin_rx_tail =
            (uint8_t)((dwin_rx_tail + 1U) % DWIN_RX_CHUNK_COUNT);
        dwin_rx_count++;
        dwin_activity_generation++;
        dwin_idle_elapsed_ms = 0U;
        dwin_idle_tracking = !frame_boundary;
        accepted = true;
    }
    else
    {
        dwin_diagnostics.rejected_chunks++;
        dwin_diagnostics.overflow_events++;
        dwin_discontinuity = true;
        dwin_idle_tracking = false;
        dwin_timeout_pending = false;
    }
    dwin_irq_unlock(NULL, state);

    (void)tx_event_flags_set(
        &dwin_events,
        accepted ? DWIN_EVENT_RX_WORK : DWIN_EVENT_OVERFLOW,
        TX_OR);
    return accepted;
}

/** @brief Mark an incomplete transfer after 20 ms of software silence. */
void dwin_ldc_channel_tick(uint32_t elapsed_ms)
{
    ldc_lock_state_t state;
    bool notify = false;

    if(!dwin_initialized || elapsed_ms == 0U)
        return;
    state = dwin_irq_lock(NULL);
    if(dwin_idle_tracking)
    {
        if(UINT32_MAX - dwin_idle_elapsed_ms < elapsed_ms)
            dwin_idle_elapsed_ms = UINT32_MAX;
        else
            dwin_idle_elapsed_ms += elapsed_ms;
        if(dwin_idle_elapsed_ms >= DWIN_IDLE_TIMEOUT_MS)
        {
            dwin_timeout_generation = dwin_activity_generation;
            dwin_timeout_pending = true;
            dwin_idle_tracking = false;
            dwin_idle_elapsed_ms = 0U;
            notify = true;
        }
    }
    dwin_irq_unlock(NULL, state);

    if(notify)
        (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_RX_WORK, TX_OR);
}

/** @brief Mark all queued data discontinuous after a DWIN UART error. */
void dwin_ldc_channel_abort(void)
{
    ldc_lock_state_t state;

    if(!dwin_initialized)
        return;
    state = dwin_irq_lock(NULL);
    dwin_discontinuity = true;
    dwin_activity_generation++;
    dwin_idle_tracking = false;
    dwin_timeout_pending = false;
    dwin_idle_elapsed_ms = 0U;
    dwin_irq_unlock(NULL, state);
    (void)tx_event_flags_set(&dwin_events, DWIN_EVENT_OVERFLOW, TX_OR);
}

/** @brief Parse and dispatch DMA publications from the single owner thread. */
void dwin_ldc_channel_owner_wait(ULONG wait_option)
{
    dwin_rx_chunk_t chunk;
    ULONG actual_flags;

    if(!dwin_initialized)
        return;
    if(tx_event_flags_get(&dwin_events,
                          DWIN_EVENT_RX_WORK | DWIN_EVENT_OVERFLOW,
                          TX_OR_CLEAR,
                          &actual_flags,
                          wait_option) != TX_SUCCESS)
    {
        return;
    }

    if(dwin_recover_discontinuity())
        return;
    while(dwin_pop_rx_chunk(&chunk))
    {
        if(!dwin_rx_parser_feed(&dwin_parser,
                                chunk.data,
                                chunk.length,
                                dwin_commit_exact_frame,
                                NULL))
        {
            dwin_ldc_channel_abort();
            (void)dwin_recover_discontinuity();
            return;
        }
        if(chunk.frame_boundary)
            (void)dwin_rx_parser_on_idle(&dwin_parser);
        if(dwin_recover_discontinuity())
            return;
    }
    dwin_apply_idle_timeout();
}

/** @brief Copy coherent LDC, channel, and parser diagnostic snapshots. */
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
    dwin_rx_parser_get_diagnostics(&dwin_parser,
                                   &channel_stats->parser);
    dwin_irq_unlock(NULL, state);
    return true;
}
