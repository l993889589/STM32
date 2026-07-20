/**
 * @file drv_dwin.h
 * @brief Low-level DWIN UART binding reserved for the dwin_tx owner.
 */

#ifndef DRV_DWIN_H
#define DRV_DWIN_H

#include <stdint.h>

#include "bsp_status.h"

/**
 * @brief Bind the DWIN UART receive and error callbacks.
 * @return BSP status from the UART callback registration.
 */
bsp_status_t drv_dwin_init(void);

/**
 * @brief Write one complete DWIN frame from the unique TX owner.
 * @param data Complete on-wire frame.
 * @param length Frame size in bytes.
 * @param timeout_ms Bounded UART transmit timeout.
 * @return BSP transport status.
 */
bsp_status_t drv_dwin_write(const uint8_t *data,
                            uint16_t length,
                            uint32_t timeout_ms);

#endif
