#include "ldc_endpoint_threadx.h"

#include <limits.h>
#include <string.h>

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 100U
#endif

static uint32_t endpoint_now_ms(void)
{
    return (uint32_t)(((uint64_t)tx_time_get() * 1000ULL) /
                      (uint64_t)TX_TIMER_TICKS_PER_SECOND);
}

static ULONG endpoint_ms_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks;

    ticks = ((uint64_t)milliseconds * TX_TIMER_TICKS_PER_SECOND + 999ULL) / 1000ULL;
    if(ticks == 0ULL)
        ticks = 1ULL;
    if(ticks > (uint64_t)ULONG_MAX)
        ticks = (uint64_t)ULONG_MAX;
    return (ULONG)ticks;
}

static uint32_t endpoint_lock(void *arg)
{
    (void)arg;
    return (uint32_t)tx_interrupt_control(TX_INT_DISABLE);
}

static void endpoint_unlock(void *arg, uint32_t state)
{
    (void)arg;
    (void)tx_interrupt_control((UINT)state);
}

static void endpoint_ldc_event(void *arg, ldc_event_t event)
{
    ldc_endpoint_t *endpoint = (ldc_endpoint_t *)arg;
    ULONG flags;

    if(!endpoint || endpoint->initialized == 0U)
        return;

    if(event == LDC_EVT_PACKET)
        flags = LDC_ENDPOINT_EVT_PACKET;
    else if(event == LDC_EVT_OVERFLOW)
        flags = LDC_ENDPOINT_EVT_OVERFLOW;
    else
        flags = LDC_ENDPOINT_EVT_DROP;

    (void)tx_event_flags_set(&endpoint->events, flags, TX_OR);
}

static void endpoint_account_time(ldc_endpoint_t *endpoint, uint32_t now_ms)
{
    uint32_t elapsed;

    elapsed = now_ms - endpoint->last_accounted_ms;
    if(elapsed != 0U)
    {
        endpoint->last_accounted_ms = now_ms;
        ldc_tick(&endpoint->ldc, elapsed);
    }
}

UINT ldc_endpoint_init(ldc_endpoint_t *endpoint, const ldc_endpoint_config_t *config)
{
    UINT status;

    if(!endpoint || !config || !config->name)
        return TX_PTR_ERROR;

    memset(endpoint, 0, sizeof(*endpoint));
    if(!ldc_init(&endpoint->ldc,
                 config->ring_buffer,
                 config->ring_size,
                 config->packet_pool,
                 config->packet_count))
        return TX_SIZE_ERROR;

    status = tx_event_flags_create(&endpoint->events, (CHAR *)config->name);
    if(status != TX_SUCCESS)
        return status;

    endpoint->name = config->name;
    endpoint->timeout_ms = config->timeout_ms;
    endpoint->last_activity_ms = endpoint_now_ms();
    endpoint->last_accounted_ms = endpoint->last_activity_ms;
    endpoint->initialized = 1U;

    ldc_set_lock(&endpoint->ldc, endpoint_lock, endpoint_unlock, endpoint);
    ldc_set_mode(&endpoint->ldc, config->mode);
    ldc_set_frame_config(&endpoint->ldc,
                         config->max_frame,
                         config->timeout_ms,
                         config->delimiter);
    ldc_set_callback(&endpoint->ldc, endpoint_ldc_event, endpoint);
    return TX_SUCCESS;
}

uint32_t ldc_endpoint_write(ldc_endpoint_t *endpoint, const uint8_t *data, uint32_t len)
{
    uint32_t written;
    uint32_t now_ms;

    if(!endpoint || endpoint->initialized == 0U)
        return 0U;

    written = ldc_write(&endpoint->ldc, data, len);
    if(written != 0U)
    {
        now_ms = endpoint_now_ms();
        endpoint->last_activity_ms = now_ms;
        endpoint->last_accounted_ms = now_ms;
        (void)tx_event_flags_set(&endpoint->events, LDC_ENDPOINT_EVT_RX_ACTIVITY, TX_OR);
    }
    return written;
}

bool ldc_endpoint_putc(ldc_endpoint_t *endpoint, uint8_t byte)
{
    return ldc_endpoint_write(endpoint, &byte, 1U) == 1U;
}

bool ldc_endpoint_flush(ldc_endpoint_t *endpoint)
{
    if(!endpoint || endpoint->initialized == 0U)
        return false;
    return ldc_flush(&endpoint->ldc);
}

int ldc_endpoint_read(ldc_endpoint_t *endpoint, uint8_t *buffer, uint32_t size)
{
    if(!endpoint || endpoint->initialized == 0U)
        return -1;
    return ldc_read_packet(&endpoint->ldc, buffer, size);
}

uint16_t ldc_endpoint_packet_count(ldc_endpoint_t *endpoint)
{
    if(!endpoint || endpoint->initialized == 0U)
        return 0U;
    return ldc_packet_available(&endpoint->ldc);
}

UINT ldc_endpoint_wait(ldc_endpoint_t *endpoint, ULONG *actual_events)
{
    ULONG wait_ticks = TX_WAIT_FOREVER;
    ULONG events = 0U;
    uint32_t now_ms;
    UINT status;

    if(!endpoint || endpoint->initialized == 0U || !actual_events)
        return TX_PTR_ERROR;

    now_ms = endpoint_now_ms();
    endpoint_account_time(endpoint, now_ms);

    if(endpoint->timeout_ms != 0U && ldc_frame_pending(&endpoint->ldc))
    {
        uint32_t silence_ms = now_ms - endpoint->last_activity_ms;
        uint32_t remain_ms = (silence_ms >= endpoint->timeout_ms) ?
                             1U : endpoint->timeout_ms - silence_ms;
        wait_ticks = endpoint_ms_to_ticks(remain_ms);
    }

    status = tx_event_flags_get(&endpoint->events,
                                LDC_ENDPOINT_EVT_ALL,
                                TX_OR_CLEAR,
                                &events,
                                wait_ticks);

    now_ms = endpoint_now_ms();
    endpoint_account_time(endpoint, now_ms);
    if(ldc_packet_available(&endpoint->ldc) != 0U)
        events |= LDC_ENDPOINT_EVT_PACKET;

    if(status == TX_NO_EVENTS && events != 0U)
        status = TX_SUCCESS;

    *actual_events = events;
    return status;
}

UINT ldc_endpoint_signal(ldc_endpoint_t *endpoint, ULONG events)
{
    if(!endpoint || endpoint->initialized == 0U)
        return TX_PTR_ERROR;
    return tx_event_flags_set(&endpoint->events, events, TX_OR);
}

bool ldc_endpoint_get_stats(ldc_endpoint_t *endpoint, ldc_stats_t *stats)
{
    if(!endpoint || endpoint->initialized == 0U)
        return false;
    return ldc_get_stats(&endpoint->ldc, stats);
}
