/**
 * @file dwin_ldc_channel.h
 * @brief Single-owner ThreadX integration for the DWIN UART LDC stream.
 */

#ifndef DWIN_LDC_CHANNEL_H
#define DWIN_LDC_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "ldc.h"
#include "tx_api.h"

/** @brief Application callback for one asynchronous DWIN frame. */
typedef void (*dwin_ldc_async_handler_t)(unsigned char *message, int length);

/** @brief Integration diagnostics in addition to the canonical LDC counters. */
typedef struct
{
    uint32_t rejected_chunks;
    uint32_t overflow_events;
    uint32_t unsolicited_acknowledgements;
    uint32_t asynchronous_frames;
} dwin_ldc_channel_diagnostics_t;

/**
 * @brief Initialize the only CHPM LDC instance and its ThreadX wait objects.
 * @param async_handler Called by the owner thread for non-acknowledgement frames.
 * @return True on success or when already initialized.
 */
bool dwin_ldc_channel_init(dwin_ldc_async_handler_t async_handler);

/**
 * @brief Feed one complete DMA segment from the DWIN UART callback.
 * @param data Segment bytes, valid for the duration of this call.
 * @param length Segment length in bytes.
 * @param frame_boundary True only for the last segment of a hardware IDLE event.
 * @return True only when the complete segment was accepted without overflow/drop.
 * @note This function is ISR-safe. The unsafe ReceiveToIdle helper is not used.
 */
bool dwin_ldc_channel_feed(const uint8_t *data,
                           uint16_t length,
                           bool frame_boundary);

/**
 * @brief Advance the application-owned DWIN idle timer.
 * @param elapsed_ms Elapsed wall time in milliseconds.
 * @note LDC owns no timer; this adapter commits after 20 ms of silence.
 */
void dwin_ldc_channel_tick(uint32_t elapsed_ms);

/** @brief Discard only the current incomplete DWIN frame after a UART error. */
void dwin_ldc_channel_abort(void);

/**
 * @brief Wait for and dispatch queued DWIN frames from the single owner thread.
 * @param wait_option ThreadX wait option such as TX_WAIT_FOREVER.
 */
void dwin_ldc_channel_owner_wait(ULONG wait_option);

/**
 * @brief Serialize one request that expects a DWIN acknowledgement.
 * @param wait_option Maximum ThreadX ticks to wait for request ownership.
 * @return True when request ownership was acquired.
 */
bool dwin_ldc_channel_request_begin(ULONG wait_option);

/**
 * @brief Wait for the owner thread to publish an acknowledgement.
 * @param buffer Destination owned by the caller.
 * @param capacity Destination capacity in bytes.
 * @param wait_option Maximum ThreadX ticks to wait.
 * @return A positive acknowledgement length, zero on timeout, or -1 on error.
 */
int dwin_ldc_channel_request_wait(uint8_t *buffer,
                                  uint16_t capacity,
                                  ULONG wait_option);

/** @brief Release serialized request ownership after success or failure. */
void dwin_ldc_channel_request_end(void);

/** @brief Copy coherent LDC and integration diagnostic snapshots. */
bool dwin_ldc_channel_get_diagnostics(
    ldc_stats_t *ldc_stats,
    dwin_ldc_channel_diagnostics_t *channel_stats);

#endif /* DWIN_LDC_CHANNEL_H */
