/**
 * @file test_dwin_rx_parser.c
 * @brief Host regression tests for DWIN length framing and recovery.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dwin_rx_parser.h"

#define TEST_FRAME_CAPACITY 4U

/** @brief Captured frames and callback behavior for one parser test. */
typedef struct
{
    uint8_t frames[TEST_FRAME_CAPACITY][DWIN_RX_MAX_FRAME_BYTES];
    uint16_t lengths[TEST_FRAME_CAPACITY];
    uint16_t count;
    bool accept;
} frame_recorder_t;

/** @brief Capture one exact parser delivery for later assertions. */
static bool record_frame(const uint8_t *frame,
                         uint16_t length,
                         void *context)
{
    frame_recorder_t *recorder = (frame_recorder_t *)context;

    assert(frame != NULL);
    assert(recorder != NULL);
    assert(length <= DWIN_RX_MAX_FRAME_BYTES);
    if(!recorder->accept || recorder->count >= TEST_FRAME_CAPACITY)
        return false;
    memcpy(recorder->frames[recorder->count], frame, length);
    recorder->lengths[recorder->count] = length;
    recorder->count++;
    return true;
}

/** @brief Initialize parser and accepting frame recorder state. */
static void initialize_test(dwin_rx_parser_t *parser,
                            frame_recorder_t *recorder)
{
    memset(recorder, 0, sizeof(*recorder));
    recorder->accept = true;
    dwin_rx_parser_init(parser);
}

/** @brief Verify one frame split across arbitrary DMA publications. */
static void test_split_frame(void)
{
    static const uint8_t frame[] =
        {0x5AU, 0xA5U, 0x05U, 0x82U, 0x4FU, 0x4BU, 0xA5U, 0xEFU};
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;

    initialize_test(&parser, &recorder);
    assert(dwin_rx_parser_feed(&parser,
                               frame,
                               4U,
                               record_frame,
                               &recorder));
    assert(recorder.count == 0U);
    assert(dwin_rx_parser_feed(&parser,
                               &frame[4],
                               (uint16_t)(sizeof(frame) - 4U),
                               record_frame,
                               &recorder));
    assert(recorder.count == 1U);
    assert(recorder.lengths[0] == sizeof(frame));
    assert(memcmp(recorder.frames[0], frame, sizeof(frame)) == 0);
}

/** @brief Verify two concatenated acknowledgements are emitted separately. */
static void test_concatenated_frames(void)
{
    static const uint8_t frames[] =
    {
        0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU,
        0x5AU, 0xA5U, 0x05U, 0x82U, 0x4FU, 0x4BU, 0xA5U, 0xEFU
    };
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;

    initialize_test(&parser, &recorder);
    assert(dwin_rx_parser_feed(&parser,
                               frames,
                               sizeof(frames),
                               record_frame,
                               &recorder));
    assert(recorder.count == 2U);
    assert(recorder.lengths[0] == 6U);
    assert(recorder.lengths[1] == 8U);
}

/** @brief Verify IDLE discards only a trailing partial candidate. */
static void test_idle_truncation_and_recovery(void)
{
    static const uint8_t complete[] =
        {0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU};
    static const uint8_t partial[] =
        {0x5AU, 0xA5U, 0x05U, 0x82U};
    uint8_t segment[sizeof(complete) + sizeof(partial)];
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;
    dwin_rx_parser_diagnostics_t diagnostics;

    memcpy(segment, complete, sizeof(complete));
    memcpy(&segment[sizeof(complete)], partial, sizeof(partial));
    initialize_test(&parser, &recorder);
    assert(dwin_rx_parser_feed(&parser,
                               segment,
                               sizeof(segment),
                               record_frame,
                               &recorder));
    assert(recorder.count == 1U);
    assert(!dwin_rx_parser_on_idle(&parser));
    assert(dwin_rx_parser_feed(&parser,
                               complete,
                               sizeof(complete),
                               record_frame,
                               &recorder));
    assert(recorder.count == 2U);
    dwin_rx_parser_get_diagnostics(&parser, &diagnostics);
    assert(diagnostics.completed_frames == 2U);
    assert(diagnostics.truncated_frames == 1U);
    assert(diagnostics.discarded_bytes == sizeof(partial));
}

/** @brief Verify noise and overlapping 5A prefixes resynchronize. */
static void test_noise_resynchronization(void)
{
    static const uint8_t stream[] =
    {
        0x00U, 0x11U, 0x5AU, 0x5AU, 0xA5U,
        0x03U, 0x82U, 0x4FU, 0x4BU
    };
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;
    dwin_rx_parser_diagnostics_t diagnostics;

    initialize_test(&parser, &recorder);
    assert(dwin_rx_parser_feed(&parser,
                               stream,
                               sizeof(stream),
                               record_frame,
                               &recorder));
    assert(recorder.count == 1U);
    dwin_rx_parser_get_diagnostics(&parser, &diagnostics);
    assert(diagnostics.discarded_bytes == 3U);
}

/** @brief Verify a LEN=255 frame occupies the full 258-byte capacity safely. */
static void test_maximum_frame(void)
{
    uint8_t frame[DWIN_RX_MAX_FRAME_BYTES];
    uint16_t index;
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;

    frame[0] = 0x5AU;
    frame[1] = 0xA5U;
    frame[2] = 0xFFU;
    for(index = 3U; index < DWIN_RX_MAX_FRAME_BYTES; index++)
        frame[index] = (uint8_t)index;

    initialize_test(&parser, &recorder);
    assert(dwin_rx_parser_feed(&parser,
                               frame,
                               sizeof(frame),
                               record_frame,
                               &recorder));
    assert(recorder.count == 1U);
    assert(recorder.lengths[0] == DWIN_RX_MAX_FRAME_BYTES);
    assert(memcmp(recorder.frames[0], frame, sizeof(frame)) == 0);
}

/** @brief Verify LEN=0 is rejected and the following frame still succeeds. */
static void test_invalid_length_recovery(void)
{
    static const uint8_t stream[] =
    {
        0x5AU, 0xA5U, 0x00U,
        0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU
    };
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;
    dwin_rx_parser_diagnostics_t diagnostics;

    initialize_test(&parser, &recorder);
    assert(dwin_rx_parser_feed(&parser,
                               stream,
                               sizeof(stream),
                               record_frame,
                               &recorder));
    assert(recorder.count == 1U);
    dwin_rx_parser_get_diagnostics(&parser, &diagnostics);
    assert(diagnostics.invalid_lengths == 1U);
    assert(diagnostics.discarded_bytes == 3U);
}

/** @brief Verify consumer rejection is visible and stops the current feed. */
static void test_delivery_failure(void)
{
    static const uint8_t frame[] =
        {0x5AU, 0xA5U, 0x03U, 0x82U, 0x4FU, 0x4BU};
    dwin_rx_parser_t parser;
    frame_recorder_t recorder;
    dwin_rx_parser_diagnostics_t diagnostics;

    initialize_test(&parser, &recorder);
    recorder.accept = false;
    assert(!dwin_rx_parser_feed(&parser,
                                frame,
                                sizeof(frame),
                                record_frame,
                                &recorder));
    dwin_rx_parser_get_diagnostics(&parser, &diagnostics);
    assert(diagnostics.delivery_failures == 1U);
    assert(diagnostics.completed_frames == 0U);
}

/** @brief Execute every DWIN receive parser host regression. */
int main(void)
{
    test_split_frame();
    test_concatenated_frames();
    test_idle_truncation_and_recovery();
    test_noise_resynchronization();
    test_maximum_frame();
    test_invalid_length_recovery();
    test_delivery_failure();
    puts("DWIN RX parser tests passed.");
    return 0;
}
