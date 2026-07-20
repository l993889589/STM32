/**
 * @file dwin_rx_parser.c
 * @brief Length-aware DWIN 5A A5 stream parsing and resynchronization.
 */

#include "dwin_rx_parser.h"

#include <stddef.h>
#include <string.h>

/** @brief Reset only the current candidate frame and preserve diagnostics. */
static void dwin_rx_parser_reset_candidate(dwin_rx_parser_t *parser)
{
    parser->length = 0U;
    parser->expected_length = 0U;
}

/** @brief Start a new candidate with the first DWIN synchronization byte. */
static void dwin_rx_parser_start_candidate(dwin_rx_parser_t *parser)
{
    parser->frame[0] = 0x5AU;
    parser->length = 1U;
    parser->expected_length = 0U;
}

/** @brief Initialize an empty DWIN stream parser. */
void dwin_rx_parser_init(dwin_rx_parser_t *parser)
{
    if(parser == NULL)
        return;
    memset(parser, 0, sizeof(*parser));
}

/** @brief Consume one ordered byte-stream segment. */
bool dwin_rx_parser_feed(dwin_rx_parser_t *parser,
                         const uint8_t *data,
                         uint16_t length,
                         dwin_rx_frame_handler_t handler,
                         void *context)
{
    uint16_t index;

    if(parser == NULL || data == NULL || length == 0U || handler == NULL)
        return false;

    for(index = 0U; index < length; index++)
    {
        uint8_t byte = data[index];

        if(parser->length == 0U)
        {
            if(byte == 0x5AU)
                dwin_rx_parser_start_candidate(parser);
            else
                parser->diagnostics.discarded_bytes++;
            continue;
        }

        if(parser->length == 1U)
        {
            if(byte == 0xA5U)
            {
                parser->frame[1] = byte;
                parser->length = 2U;
            }
            else if(byte == 0x5AU)
            {
                parser->diagnostics.discarded_bytes++;
            }
            else
            {
                parser->diagnostics.discarded_bytes += 2U;
                dwin_rx_parser_reset_candidate(parser);
            }
            continue;
        }

        if(parser->length == 2U)
        {
            if(byte == 0U)
            {
                parser->diagnostics.invalid_lengths++;
                parser->diagnostics.discarded_bytes += 3U;
                dwin_rx_parser_reset_candidate(parser);
                continue;
            }
            parser->frame[2] = byte;
            parser->length = 3U;
            parser->expected_length = (uint16_t)(3U + byte);
            continue;
        }

        parser->frame[parser->length++] = byte;
        if(parser->length == parser->expected_length)
        {
            bool accepted = handler(parser->frame,
                                    parser->length,
                                    context);

            if(accepted)
                parser->diagnostics.completed_frames++;
            else
                parser->diagnostics.delivery_failures++;
            dwin_rx_parser_reset_candidate(parser);
            if(!accepted)
                return false;
        }
    }
    return true;
}

/** @brief Discard a partial frame when hardware reports line silence. */
bool dwin_rx_parser_on_idle(dwin_rx_parser_t *parser)
{
    if(parser == NULL)
        return false;
    if(parser->length == 0U)
        return true;

    parser->diagnostics.truncated_frames++;
    parser->diagnostics.discarded_bytes += parser->length;
    dwin_rx_parser_reset_candidate(parser);
    return false;
}

/** @brief Discard the partial frame after a transport discontinuity. */
void dwin_rx_parser_abort(dwin_rx_parser_t *parser)
{
    if(parser == NULL)
        return;
    if(parser->length > 0U)
    {
        parser->diagnostics.truncated_frames++;
        parser->diagnostics.discarded_bytes += parser->length;
    }
    dwin_rx_parser_reset_candidate(parser);
}

/** @brief Copy parser diagnostic counters. */
void dwin_rx_parser_get_diagnostics(
    const dwin_rx_parser_t *parser,
    dwin_rx_parser_diagnostics_t *diagnostics)
{
    if(parser == NULL || diagnostics == NULL)
        return;
    *diagnostics = parser->diagnostics;
}
