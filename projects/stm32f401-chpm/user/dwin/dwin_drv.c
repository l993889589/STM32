/**
 * @file dwin_drv.c
 * @brief Legacy DWIN write adapter routed through the unique TX owner.
 */

#include "dwin_drv.h"

#include "dwin_tx.h"

/**
 * @brief Submit one ordered DWIN variable write through dwin_tx.
 *
 * The historical function name implied a synchronous wait. The current
 * implementation returns after static queue admission so no caller can own or
 * misassociate the screen's fixed ACK.
 */
uint8_t dwin_write_block(uint16_t address,
                         uint8_t *data,
                         uint8_t length,
                         uint16_t timeout_ms)
{
    (void)timeout_ms;

    return dwin_tx_submit_write_event(address,
                                      data,
                                      length,
                                      2U) == DWIN_TX_STATUS_OK ?
           1U : 0U;
}
