#include "app_ldc_config.h"

#include <stddef.h>

#include "app_config.h"

static const app_ldc_port_config_t g_ldc_ports[APP_LDC_PORT_COUNT] =
{
    {
        APP_LDC_PORT_USB_CDC,
        "usb-cdc",
        APP_LDC_LINK_USB_CDC,
        0U,
        APP_USB_LDC_MAX_FRAME,
        {
            LDC_FRAME_POLICY_LENGTH_TIMEOUT,
            APP_USB_LDC_MAX_FRAME,
            20U,
            '\n',
            0U
        }
    },
    {
        APP_LDC_PORT_RS485,
        "rs485-modbus",
        APP_LDC_LINK_UART,
        APP_RS485_UART_BAUDRATE,
        APP_RS485_LDC_MAX_FRAME,
        {
            LDC_FRAME_POLICY_MODBUS_RTU,
            APP_RS485_LDC_MAX_FRAME,
            0U,
            -1,
            APP_RS485_UART_BAUDRATE
        }
    },
    {
        APP_LDC_PORT_W800_AT,
        "w800-at",
        APP_LDC_LINK_UART,
        APP_W800_UART_BAUDRATE,
        256U,
        {
            LDC_FRAME_POLICY_LENGTH_TIMEOUT,
            256U,
            20U,
            '\n',
            APP_W800_UART_BAUDRATE
        }
    }
};

const app_ldc_port_config_t *app_ldc_config_get(app_ldc_port_id_t id)
{
    if((uint32_t)id >= APP_LDC_PORT_COUNT)
        return NULL;

    return &g_ldc_ports[id];
}

void app_ldc_config_apply(ldc_t *ldc, app_ldc_port_id_t id)
{
    const app_ldc_port_config_t *cfg = app_ldc_config_get(id);

    if(!ldc || !cfg)
        return;

    ldc_frame_policy_apply(ldc, &cfg->policy);
}
