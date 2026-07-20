/**
 * @file drv_dwin.c
 * @brief Thin DWIN UART binding used only by the dwin_tx owner service.
 */

#include "drv_dwin.h"

#include "bsp_uart.h"
#include "dwin_ldc_channel.h"

/** @brief Forward UART receive segments into the single DWIN LDC channel. */
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

/** @brief Abort the active DWIN receive/request state after a UART error. */
static void dwin_error(void *context)
{
    (void)context;
    dwin_ldc_channel_abort();
}

/** @brief Attach DWIN UART callbacks to the LDC receive channel. */
bsp_status_t drv_dwin_init(void)
{
    return bsp_uart_set_callbacks(BSP_UART_DWIN,
                                  dwin_rx,
                                  dwin_error,
                                  NULL);
}

/** @brief Perform the low-level DWIN UART write for the unique TX owner. */
bsp_status_t drv_dwin_write(const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms)
{
    return bsp_uart_write(BSP_UART_DWIN, data, length, timeout_ms);
}
