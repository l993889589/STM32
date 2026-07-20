/**
 * @file dwin_ldc_channel.h
 * @brief Single-owner ThreadX integration for the DWIN UART LDC stream.
 */

#ifndef DWIN_LDC_CHANNEL_H
#define DWIN_LDC_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#include "dwin_rx_parser.h"
#include "ldc.h"
#include "tx_api.h"

/** @brief Application callback for one asynchronous DWIN frame. */
typedef void (*dwin_ldc_async_handler_t)(unsigned char *message, int length);

/** @brief Integration diagnostics in addition to the canonical LDC counters. */
typedef struct
{
    uint32_t rejected_chunks;
    uint32_t overflow_events;
    uint32_t acknowledgements_plain;
    uint32_t acknowledgements_crc;
    uint32_t acknowledgement_activity;
    uint32_t asynchronous_frames;
    dwin_rx_parser_diagnostics_t parser;
} dwin_ldc_channel_diagnostics_t;

/**
 * @brief Initialize the only CHPM LDC instance and its ThreadX wait objects.
 * @param async_handler Called by the owner thread for non-acknowledgement frames.
 * @return True on success or when already initialized.
 */
bool dwin_ldc_channel_init(dwin_ldc_async_handler_t async_handler);

/**
 * @brief Copy one DMA segment into the bounded ISR-to-owner queue.
 * @param data Segment bytes, valid for the duration of this call.
 * @param length Segment length in bytes.
 * @param frame_boundary True only for the last segment of a hardware IDLE event.
 * @return True only when the complete segment entered the static queue.
 * @note This function is ISR-safe and performs no protocol parsing.
 */
bool dwin_ldc_channel_feed(const uint8_t *data,
                           uint16_t length,
                           bool frame_boundary);

/**
 * @brief Advance the fallback DWIN idle timer.
 * @param elapsed_ms Elapsed wall time in milliseconds.
 * @note Silence discards only an incomplete parser candidate; it never frames
 *       a complete message.
 */
void dwin_ldc_channel_tick(uint32_t elapsed_ms);

/** @brief Mark queued data discontinuous after a DWIN UART error. */
void dwin_ldc_channel_abort(void);

/**
 * @brief Parse and dispatch queued DWIN bytes from the single owner thread.
 * @param wait_option ThreadX wait option such as TX_WAIT_FOREVER.
 */
void dwin_ldc_channel_owner_wait(ULONG wait_option);

/** @brief Copy coherent LDC and integration diagnostic snapshots. */
bool dwin_ldc_channel_get_diagnostics(
    ldc_stats_t *ldc_stats,
    dwin_ldc_channel_diagnostics_t *channel_stats);

#endif /* DWIN_LDC_CHANNEL_H */
