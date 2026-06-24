#include "usb_vendor_transport.h"

#include <stddef.h>
#include <string.h>

#include "ux_device_stack.h"

#define USB_VENDOR_TX_WAIT_TICKS  200U

static TX_MUTEX g_vendor_tx_mutex;
static UX_SLAVE_CLASS_DPUMP *g_vendor_instance;
static usb_vendor_parser_t g_vendor_parser;
static uint8_t g_vendor_tx_frame[USB_VENDOR_MAX_FRAME];
static uint8_t g_vendor_initialized;

UINT usb_vendor_transport_init(usb_vendor_frame_fn handler, void *handler_arg)
{
    if(g_vendor_initialized == 0U)
    {
        if(tx_mutex_create(&g_vendor_tx_mutex, "usb vendor tx", TX_INHERIT) != TX_SUCCESS)
            return TX_MUTEX_ERROR;
        g_vendor_initialized = 1U;
    }

    usb_vendor_parser_init(&g_vendor_parser, handler, handler_arg);
    return TX_SUCCESS;
}

void usb_vendor_transport_activate(void *instance)
{
    g_vendor_instance = (UX_SLAVE_CLASS_DPUMP *)instance;
}

void usb_vendor_transport_deactivate(void *instance)
{
    if(g_vendor_instance == (UX_SLAVE_CLASS_DPUMP *)instance)
        g_vendor_instance = UX_NULL;
}

bool usb_vendor_transport_is_connected(void)
{
    return g_vendor_instance != UX_NULL;
}

UINT usb_vendor_transport_read(uint8_t *data, uint32_t capacity, uint32_t *actual)
{
    UX_SLAVE_CLASS_DPUMP *instance = g_vendor_instance;
    UX_SLAVE_ENDPOINT *endpoint;
    UX_SLAVE_TRANSFER *transfer;
    ULONG requested;
    ULONG received;
    UINT status;

    if(actual != NULL)
        *actual = 0U;
    if(instance == UX_NULL || data == NULL || capacity == 0U || actual == NULL)
        return UX_ERROR;

    endpoint = instance->ux_slave_class_dpump_bulkout_endpoint;
    if(endpoint == UX_NULL)
        return UX_ERROR;

    requested = endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize;
    if(requested == 0U || requested > capacity)
        requested = capacity;
    if(requested > UX_SLAVE_REQUEST_DATA_MAX_LENGTH)
        requested = UX_SLAVE_REQUEST_DATA_MAX_LENGTH;

    /* DPUMP read waits for the full requested length. Return after one USB
       transaction so short LDV1 frames reach the stream parser immediately. */
    transfer = &endpoint->ux_slave_endpoint_transfer_request;
    status = _ux_device_stack_transfer_request(transfer, requested, requested);
    received = transfer->ux_slave_transfer_request_actual_length;
    if(status != UX_SUCCESS || received > requested)
        return status != UX_SUCCESS ? status : UX_ERROR;

    if(received != 0U)
        memcpy(data, transfer->ux_slave_transfer_request_data_pointer, received);
    *actual = (uint32_t)received;
    return UX_SUCCESS;
}

void usb_vendor_transport_feed(const uint8_t *data, uint32_t length)
{
    usb_vendor_parser_feed(&g_vendor_parser, data, length);
}

UINT usb_vendor_transport_send(uint8_t channel,
                               uint8_t flags,
                               uint16_t sequence,
                               const uint8_t *payload,
                               uint32_t payload_length)
{
    UX_SLAVE_CLASS_DPUMP *instance;
    uint32_t frame_length;
    ULONG actual = 0U;
    UINT status;

    if(g_vendor_initialized == 0U)
        return UX_ERROR;
    if(tx_mutex_get(&g_vendor_tx_mutex, USB_VENDOR_TX_WAIT_TICKS) != TX_SUCCESS)
        return UX_ERROR;

    instance = g_vendor_instance;
    if(instance == UX_NULL)
    {
        (void)tx_mutex_put(&g_vendor_tx_mutex);
        return UX_ERROR;
    }

    frame_length = usb_vendor_frame_encode(g_vendor_tx_frame,
                                            sizeof(g_vendor_tx_frame),
                                            channel,
                                            flags,
                                            sequence,
                                            payload,
                                            payload_length);
    if(frame_length == 0U)
    {
        (void)tx_mutex_put(&g_vendor_tx_mutex);
        return UX_ERROR;
    }

    status = ux_device_class_dpump_write(instance, g_vendor_tx_frame, frame_length, &actual);
    if(status == UX_SUCCESS && actual == frame_length &&
       instance->ux_slave_class_dpump_bulkin_endpoint != UX_NULL &&
       (frame_length % instance->ux_slave_class_dpump_bulkin_endpoint->ux_slave_endpoint_descriptor.wMaxPacketSize) == 0U)
    {
        UX_SLAVE_TRANSFER *transfer =
            &instance->ux_slave_class_dpump_bulkin_endpoint->ux_slave_endpoint_transfer_request;
        status = _ux_device_stack_transfer_request(transfer, 0U, 0U);
    }
    (void)tx_mutex_put(&g_vendor_tx_mutex);
    return (status == UX_SUCCESS && actual == frame_length) ? UX_SUCCESS : UX_ERROR;
}

void usb_vendor_transport_get_parser_stats(uint32_t *frames,
                                           uint32_t *crc_errors,
                                           uint32_t *length_errors,
                                           uint32_t *discarded_bytes)
{
    if(frames != NULL)
        *frames = g_vendor_parser.frames;
    if(crc_errors != NULL)
        *crc_errors = g_vendor_parser.crc_errors;
    if(length_errors != NULL)
        *length_errors = g_vendor_parser.length_errors;
    if(discarded_bytes != NULL)
        *discarded_bytes = g_vendor_parser.discarded_bytes;
}
