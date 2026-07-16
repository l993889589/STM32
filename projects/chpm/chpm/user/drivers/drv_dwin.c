#include "drv_dwin.h"

#include "bsp_uart.h"
#include "dwin_ldc_channel.h"

static void dwin_rx(const uint8_t *data,
                    uint16_t length,
                    const bsp_uart_rx_info_t *info,
                    void *context)
{
    (void)context;
    (void)dwin_ldc_channel_feed(
        data,
        length,
        info != NULL && info->last_segment &&
            info->event == BSP_UART_RX_EVENT_IDLE);
}

static void dwin_error(void *context)
{
    (void)context;
    dwin_ldc_channel_abort();
}

bsp_status_t drv_dwin_init(void)
{
    return bsp_uart_set_callbacks(BSP_UART_DWIN,
                                  dwin_rx,
                                  dwin_error,
                                  NULL);
}

bsp_status_t drv_dwin_write(const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms)
{
    return bsp_uart_write(BSP_UART_DWIN, data, length, timeout_ms);
}
