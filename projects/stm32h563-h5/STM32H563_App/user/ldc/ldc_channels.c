#include "ldc_channels.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ldc_core.h"
#include "ldc_port_irq.h"
#include "main.h"
#include "osal.h"
#include "app_rx_handlers.h"

#define DEBUG_PACKET_SIZE 128U
#define USB_PACKET_SIZE   512U
#define DWIN_PACKET_SIZE  256U
#define MODBUS_PACKET_SIZE 256U
#define MSG_PACKET_SIZE   128U
#define PACKET_NODE_COUNT 2U
#define TASK_PACKET_SIZE  USB_PACKET_SIZE

typedef void (*queue_handler_t)(unsigned char *message, int length);

typedef struct
{
    ldc_t ldc;
    bool initialized;
    queue_handler_t handler;
} ldc_channel_t;

static ldc_channel_t debug_channel;
static uint8_t debug_ring[DEBUG_PACKET_SIZE * 2U];
static ldc_packet_t debug_packets[PACKET_NODE_COUNT];

static ldc_channel_t usb_channel;
static uint8_t usb_ring[USB_PACKET_SIZE * 2U];
static ldc_packet_t usb_packets[PACKET_NODE_COUNT];

static ldc_channel_t dwin_channel;
static uint8_t dwin_ring[DWIN_PACKET_SIZE * 2U];
static ldc_packet_t dwin_packets[PACKET_NODE_COUNT];

static ldc_channel_t modbus_channel;
static uint8_t modbus_ring[MODBUS_PACKET_SIZE * 2U];
static ldc_packet_t modbus_packets[PACKET_NODE_COUNT];
static uint8_t modbus_rx_cache[MODBUS_PACKET_SIZE];
static uint16_t modbus_rx_length;
static uint16_t modbus_rx_offset;

static ldc_channel_t msg_channel;
static uint8_t msg_ring[MSG_PACKET_SIZE * 2U];
static ldc_packet_t msg_packets[PACKET_NODE_COUNT];

static void channel_init(ldc_channel_t *channel,
                         uint8_t *ring,
                         uint32_t ring_size,
                         ldc_packet_t *packets,
                         uint16_t packet_count,
                         uint32_t max_length,
                         uint32_t timeout_ms,
                         queue_handler_t handler)
{
    ldc_init(&channel->ldc, ring, ring_size, packets, packet_count);
    ldc_set_frame_config(&channel->ldc, max_length, timeout_ms, -1);
    ldc_set_mode(&channel->ldc, LDC_MODE_PROTECT);
    ldc_set_lock(&channel->ldc, ldc_port_irq_lock, ldc_port_irq_unlock, NULL);
    channel->handler = handler;
    channel->initialized = true;
}

static void channel_tick(ldc_channel_t *channel, uint32_t elapsed_ms)
{
    if(channel->initialized)
        ldc_tick(&channel->ldc, elapsed_ms);
}

static void channel_task(ldc_channel_t *channel)
{
    uint8_t packet[TASK_PACKET_SIZE];
    int length;

    if(!channel->initialized)
        return;

    length = ldc_read_packet(&channel->ldc, packet, sizeof(packet));
    if(length > 0 && channel->handler)
        channel->handler(packet, length);
}

static int channel_read(ldc_channel_t *channel, uint8_t *buf, int size, int timeout_ms)
{
    uint32_t start_ms;
    int length;

    if(!channel->initialized || !buf || size <= 0)
        return -1;

    if(timeout_ms <= 0)
        return ldc_read_packet(&channel->ldc, buf, (uint32_t)size);

    start_ms = osal_time_ms();

    do
    {
        length = ldc_read_packet(&channel->ldc, buf, (uint32_t)size);
        if(length != 0)
            return length;
        osal_delay_ms(1U);
    } while(!osal_timeout_expired(start_ms, (uint32_t)timeout_ms));

    return 0;
}

static void channel_write(ldc_channel_t *channel, const uint8_t *buf, uint32_t length)
{
    if(channel->initialized && buf && length)
    {
        if(ldc_write(&channel->ldc, buf, length) != length)
            (void)ldc_discard_frame(&channel->ldc);
    }
}

static void channel_flush(ldc_channel_t *channel)
{
    if(channel->initialized)
        (void)ldc_flush(&channel->ldc);
}

#define DEFINE_LDC_CHANNEL(prefix, channel, ring, packets, max_len, timeout, handler) \
    void prefix##_init(void) \
    { \
        channel_init(&(channel), (ring), sizeof(ring), (packets), PACKET_NODE_COUNT, \
                     (max_len), (timeout), (handler)); \
    } \
    void prefix##_tick(void) { channel_tick(&(channel), 1U); } \
    void prefix##_task(void) { channel_task(&(channel)); } \
    int prefix##_blocking_read(uint8_t *buf, int size, int timeout_ms) \
    { \
        return channel_read(&(channel), buf, size, timeout_ms); \
    } \
    void prefix##_message_in(unsigned char *buf) \
    { \
        channel_write(&(channel), buf, 1U); \
    } \
    void prefix##_message_in_ex(unsigned char *buf, unsigned long len) \
    { \
        channel_write(&(channel), buf, (uint32_t)len); \
    }

DEFINE_LDC_CHANNEL(debug_uart_queue, debug_channel, debug_ring, debug_packets,
                   DEBUG_PACKET_SIZE, 30U, debug_uart_queue_handler)
DEFINE_LDC_CHANNEL(usb_uart_queue, usb_channel, usb_ring, usb_packets,
                   USB_PACKET_SIZE, 30U, usb_uart_queue_handler)
DEFINE_LDC_CHANNEL(dwin_uart_queue, dwin_channel, dwin_ring, dwin_packets,
                   DWIN_PACKET_SIZE, 20U, dwin_uart_queue_handler)
DEFINE_LDC_CHANNEL(modbus_queue, modbus_channel, modbus_ring, modbus_packets,
                   MODBUS_PACKET_SIZE, 2U, modbus_queue_handler)
DEFINE_LDC_CHANNEL(msg_queue, msg_channel, msg_ring, msg_packets,
                   MSG_PACKET_SIZE, 30U, msg_queue_handler)

void dwin_queue_settle(void)
{
    channel_flush(&dwin_channel);
}

void dwin_queue_abort_frame(void)
{
    if(dwin_channel.initialized)
        (void)ldc_discard_frame(&dwin_channel.ldc);
}

void modbus_queue_settle(void)
{
    channel_flush(&modbus_channel);
    modbus_rx_length = 0U;
    modbus_rx_offset = 0U;
}

void modbus_queue_frame_boundary(void)
{
    channel_flush(&modbus_channel);
}

void modbus_queue_rx_activity(void)
{
    if(modbus_channel.initialized)
        ldc_rx_activity(&modbus_channel.ldc);
}

void modbus_queue_abort_frame(void)
{
    if(modbus_channel.initialized)
        (void)ldc_discard_frame(&modbus_channel.ldc);
}

void modbus_queue_set_baud(uint32_t baud_rate)
{
    uint32_t silence_ms;

    if(!modbus_channel.initialized || baud_rate == 0U)
        return;

    /* 8N1 is 10 bits/character. Add one RTOS tick so the frame is never cut early. */
    silence_ms = (uint32_t)((35000ULL + baud_rate - 1U) / baud_rate) + 1U;
    if(silence_ms < 2U)
        silence_ms = 2U;
    ldc_set_frame_config(&modbus_channel.ldc,
                         MODBUS_PACKET_SIZE,
                         silence_ms,
                         -1);
}

bool dwin_queue_get_stats(ldc_stats_t *stats)
{
    return dwin_channel.initialized &&
           ldc_get_stats(&dwin_channel.ldc, stats);
}

bool modbus_queue_get_stats(ldc_stats_t *stats)
{
    return modbus_channel.initialized &&
           ldc_get_stats(&modbus_channel.ldc, stats);
}

int modbus_queue_read_bytes(uint8_t *buf, int size, int timeout_ms)
{
    uint32_t start_ms = osal_time_ms();

    if(!buf || size <= 0 || !modbus_channel.initialized)
        return -1;
    for(;;)
    {
        if(modbus_rx_offset < modbus_rx_length)
        {
            uint16_t available = modbus_rx_length - modbus_rx_offset;
            uint16_t count = available < (uint16_t)size ? available : (uint16_t)size;
            memcpy(buf, &modbus_rx_cache[modbus_rx_offset], count);
            modbus_rx_offset += count;
            if(modbus_rx_offset == modbus_rx_length)
            {
                modbus_rx_offset = 0U;
                modbus_rx_length = 0U;
            }
            return count;
        }

        {
            int length = ldc_read_packet(&modbus_channel.ldc,
                                         modbus_rx_cache,
                                         sizeof(modbus_rx_cache));
            if(length > 0)
            {
                modbus_rx_length = (uint16_t)length;
                modbus_rx_offset = 0U;
                continue;
            }
            if(length < 0)
                return -1;
        }

        if(timeout_ms == 0)
            return 0;
        if(timeout_ms > 0 &&
           osal_timeout_expired(start_ms, (uint32_t)timeout_ms))
            return 0;
        osal_delay_ms(1U);
    }
}
