/**
 * @file app_serial_stats.h
 * @brief Transport-neutral serial framing diagnostics exported by App services.
 */

#ifndef APP_SERIAL_STATS_H
#define APP_SERIAL_STATS_H

#include <stdint.h>

/** @brief Stable application-facing framing counters independent of LDC ABI. */
typedef struct
{
    uint64_t rx_bytes;
    uint64_t packets;
    uint64_t overflow;
    uint64_t drop;
    uint64_t overwrite_count;
    uint64_t max_used;
    uint64_t cur_used;
    uint64_t packet_used;
    uint64_t packet_peak;
} app_serial_stats_t;

#endif /* APP_SERIAL_STATS_H */
