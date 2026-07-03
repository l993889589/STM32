#include "app_ldc_config.h"

#include <stddef.h>

#include "app_config.h"

/*
 * Framing policy table only. Transport ownership, buffers, ThreadX events and
 * protocol consumers belong to the corresponding application service.
 */
static const app_ldc_port_config_t g_ldc_ports[APP_LDC_PORT_COUNT] =
{
    [APP_LDC_PORT_USB_CDC] = {
        APP_LDC_PORT_USB_CDC,
        "usb-cdc",
        APP_LDC_LINK_USB_CDC,
        0U,
        APP_USB_LDC_MAX_FRAME,
        20U,
        '\n'
    },
    [APP_LDC_PORT_UART4_ECHO] = {
        APP_LDC_PORT_UART4_ECHO,
        "uart4-debug",
        APP_LDC_LINK_UART,
        APP_UART4_ECHO_BAUDRATE,
        APP_UART4_ECHO_LDC_MAX_FRAME,
        APP_UART4_ECHO_LDC_TIMEOUT_MS,
        -1
    },
    [APP_LDC_PORT_AP6212_BT] = {
        APP_LDC_PORT_AP6212_BT,
        "ap6212-bt",
        APP_LDC_LINK_UART,
        APP_AP6212_BT_UART_BAUDRATE,
        APP_AP6212_BT_LDC_MAX_FRAME,
        APP_AP6212_BT_LDC_TIMEOUT_MS,
        -1
    },
    [APP_LDC_PORT_RS485] = {
        APP_LDC_PORT_RS485,
        "rs485-modbus",
        APP_LDC_LINK_UART,
        APP_RS485_UART_BAUDRATE,
        APP_RS485_LDC_MAX_FRAME,
        APP_RS485_FRAME_GAP_MS,
        -1
    },
    [APP_LDC_PORT_W800_AT] = {
        APP_LDC_PORT_W800_AT,
        "w800-at",
        APP_LDC_LINK_UART,
        APP_W800_UART_BAUDRATE,
        256U,
        20U,
        '\n'
    },
    [APP_LDC_PORT_NEARLINK_AT] = {
        APP_LDC_PORT_NEARLINK_AT,
        "nearlink-at",
        APP_LDC_LINK_UART,
        APP_NEARLINK_UART_BAUDRATE,
        APP_NEARLINK_LDC_MAX_FRAME,
        20U,
        '\n'
    }
};

const app_ldc_port_config_t *app_ldc_config_get(app_ldc_port_id_t id)
{
    if((uint32_t)id >= APP_LDC_PORT_COUNT)
        return NULL;

    return &g_ldc_ports[id];
}
