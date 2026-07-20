/**
 * @file dwin_app.h
 * @brief Small compatibility API for application-originated DWIN commands.
 */

#ifndef DWIN_APP_H
#define DWIN_APP_H

#include <stdint.h>

#define PAG_MAX 2U

/** @brief Start the reliable periodic DWIN buzzer schedule. */
void dwin_buzzer(void);

/** @brief Submit a PWM percentage to DWIN VP 0x1116. */
uint8_t dwin_set_pwm(uint8_t duty);

/** @brief Submit an ordered DWIN page switch. */
uint8_t dwin_set_page(uint8_t page);

#endif /* DWIN_APP_H */
