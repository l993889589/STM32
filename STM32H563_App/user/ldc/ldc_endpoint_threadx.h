#ifndef LDC_ENDPOINT_THREADX_H
#define LDC_ENDPOINT_THREADX_H

#include <stdbool.h>
#include <stdint.h>

#include "ldc_core.h"
#include "tx_api.h"

enum
{
    LDC_ENDPOINT_EVT_PACKET = 1UL << 0,
    LDC_ENDPOINT_EVT_RX_ACTIVITY = 1UL << 1,
    LDC_ENDPOINT_EVT_OVERFLOW = 1UL << 2,
    LDC_ENDPOINT_EVT_DROP = 1UL << 3,
    LDC_ENDPOINT_EVT_STOP = 1UL << 4,
    LDC_ENDPOINT_EVT_ALL = (1UL << 5) - 1UL
};

typedef struct
{
    const char *name;
    uint8_t *ring_buffer;
    uint32_t ring_size;
    ldc_packet_t *packet_pool;
    uint16_t packet_count;
    uint32_t max_frame;
    uint32_t timeout_ms;
    int delimiter;
    ldc_mode_t mode;
} ldc_endpoint_config_t;

typedef struct
{
    ldc_t ldc;
    TX_EVENT_FLAGS_GROUP events;
    const char *name;
    uint32_t timeout_ms;
    volatile uint32_t last_activity_ms;
    volatile uint32_t last_accounted_ms;
    uint8_t initialized;
} ldc_endpoint_t;

UINT ldc_endpoint_init(ldc_endpoint_t *endpoint, const ldc_endpoint_config_t *config);
uint32_t ldc_endpoint_write(ldc_endpoint_t *endpoint, const uint8_t *data, uint32_t len);
bool ldc_endpoint_putc(ldc_endpoint_t *endpoint, uint8_t byte);
bool ldc_endpoint_flush(ldc_endpoint_t *endpoint);
int ldc_endpoint_read(ldc_endpoint_t *endpoint, uint8_t *buffer, uint32_t size);
uint16_t ldc_endpoint_packet_count(ldc_endpoint_t *endpoint);
UINT ldc_endpoint_wait(ldc_endpoint_t *endpoint, ULONG *actual_events);
UINT ldc_endpoint_signal(ldc_endpoint_t *endpoint, ULONG events);
bool ldc_endpoint_get_stats(ldc_endpoint_t *endpoint, ldc_stats_t *stats);

#endif
