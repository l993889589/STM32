/**
 * @file dwin_rx_parser.h
 * @brief Pure-C length-aware parser for a DWIN 5A A5 byte stream.
 */

#ifndef DWIN_RX_PARSER_H
#define DWIN_RX_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "dwin_protocol.h"

#define DWIN_RX_MAX_FRAME_BYTES DWIN_PROTOCOL_MAX_FRAME_BYTES

/** @brief Parser-level counters for resynchronization and frame delivery. */
typedef struct
{
    uint32_t completed_frames;
    uint32_t discarded_bytes;
    uint32_t invalid_lengths;
    uint32_t truncated_frames;
    uint32_t delivery_failures;
} dwin_rx_parser_diagnostics_t;

/** @brief Static state for one independent DWIN receive stream. */
typedef struct
{
    uint8_t frame[DWIN_RX_MAX_FRAME_BYTES];
    uint16_t length;
    uint16_t expected_length;
    dwin_rx_parser_diagnostics_t diagnostics;
} dwin_rx_parser_t;

/**
 * @brief Receive one exact DWIN frame from the parser.
 * @param frame Complete frame; valid only for the duration of the callback.
 * @param length Complete frame length in bytes.
 * @param context Caller-owned callback context.
 * @return True when the consumer accepted the frame.
 */
typedef bool (*dwin_rx_frame_handler_t)(const uint8_t *frame,
                                        uint16_t length,
                                        void *context);

/**
 * @brief Initialize an empty DWIN stream parser.
 * @param parser Caller-owned static parser state.
 */
void dwin_rx_parser_init(dwin_rx_parser_t *parser);

/**
 * @brief Consume one ordered byte-stream segment.
 * @param parser Initialized parser state.
 * @param data Segment bytes; copied or delivered before return.
 * @param length Segment length in bytes.
 * @param handler Complete-frame consumer.
 * @param context Consumer context passed through unchanged.
 * @return True when every complete frame was accepted by the consumer.
 */
bool dwin_rx_parser_feed(dwin_rx_parser_t *parser,
                         const uint8_t *data,
                         uint16_t length,
                         dwin_rx_frame_handler_t handler,
                         void *context);

/**
 * @brief Apply a hardware IDLE boundary as incomplete-frame recovery.
 * @param parser Initialized parser state.
 * @return True when no partial DWIN frame had to be discarded.
 * @note Complete frames are emitted by length before this function is called.
 */
bool dwin_rx_parser_on_idle(dwin_rx_parser_t *parser);

/**
 * @brief Discard the current partial frame after transport discontinuity.
 * @param parser Initialized parser state.
 */
void dwin_rx_parser_abort(dwin_rx_parser_t *parser);

/**
 * @brief Copy parser diagnostic counters.
 * @param parser Initialized parser state.
 * @param diagnostics Caller-owned destination.
 */
void dwin_rx_parser_get_diagnostics(
    const dwin_rx_parser_t *parser,
    dwin_rx_parser_diagnostics_t *diagnostics);

#endif /* DWIN_RX_PARSER_H */
