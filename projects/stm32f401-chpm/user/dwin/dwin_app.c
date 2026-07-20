/**
 * @file dwin_app.c
 * @brief Compatibility helpers that submit display intent to dwin_tx.
 */

#include "dwin_app.h"

#include "dwin_tx.h"

/** @brief Start the latched, reliable five-second buzzer schedule. */
void dwin_buzzer(void)
{
    (void)dwin_tx_set_buzzer(true);
}

/** @brief Submit the newest PWM percentage to DWIN VP 0x1116. */
uint8_t dwin_set_pwm(uint8_t duty)
{
    uint8_t payload[2] = {0x00U, duty};

    if(duty > 100U)
        payload[1] = 100U;

    return dwin_tx_submit_write_latest(0x1116U,
                                       payload,
                                       sizeof(payload)) ==
                   DWIN_TX_STATUS_OK ?
           1U : 0U;
}

/** @brief Submit an ordered DWIN page switch command. */
uint8_t dwin_set_page(uint8_t page)
{
    uint8_t payload[4] = {0x5AU, 0x01U, 0x00U, page};

    if(page > PAG_MAX)
        return 0U;

    return dwin_tx_submit_write_event(0x0084U,
                                      payload,
                                      sizeof(payload),
                                      2U) == DWIN_TX_STATUS_OK ?
           1U : 0U;
}
