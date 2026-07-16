/**
 * @file usb_console.c
 * @brief Serialized USBX CDC console writer and active-instance binding.
 */

#include "usb_console.h"

#define USB_CONSOLE_TX_WAIT_TICKS  100U

static TX_MUTEX g_usb_console_tx_mutex;
static UX_SLAVE_CLASS_CDC_ACM *g_usb_console_instance;
static uint8_t g_usb_console_initialized;

UINT usb_console_init(void)
{
    if(g_usb_console_initialized != 0U)
        return TX_SUCCESS;

    if(tx_mutex_create(&g_usb_console_tx_mutex, "usb console tx", TX_INHERIT) != TX_SUCCESS)
        return TX_MUTEX_ERROR;

    g_usb_console_initialized = 1U;
    return TX_SUCCESS;
}

void usb_console_activate(UX_SLAVE_CLASS_CDC_ACM *instance)
{
    g_usb_console_instance = instance;
}

void usb_console_deactivate(UX_SLAVE_CLASS_CDC_ACM *instance)
{
    if(g_usb_console_instance == instance)
        g_usb_console_instance = UX_NULL;
}

UX_SLAVE_CLASS_CDC_ACM *usb_console_instance(void)
{
    return g_usb_console_instance;
}

bool usb_console_is_connected(void)
{
    return g_usb_console_instance != UX_NULL;
}

UINT usb_console_write(const uint8_t *data, uint32_t length)
{
    UX_SLAVE_CLASS_CDC_ACM *instance = g_usb_console_instance;
    ULONG actual = 0U;
    UINT status;

    if(g_usb_console_initialized == 0U || instance == UX_NULL || data == NULL || length == 0U)
        return UX_ERROR;
    if(tx_mutex_get(&g_usb_console_tx_mutex, USB_CONSOLE_TX_WAIT_TICKS) != TX_SUCCESS)
        return UX_ERROR;

    status = ux_device_class_cdc_acm_write(instance, (UCHAR *)data, length, &actual);
    (void)tx_mutex_put(&g_usb_console_tx_mutex);

    return (status == UX_SUCCESS && actual == length) ? UX_SUCCESS : UX_ERROR;
}
