/**
 * @file bsp_beep.h
 * @brief ART-Pi H750 buzzer BSP interface.
 */

#ifndef BSP_BEEP_H
#define BSP_BEEP_H

#include <stdint.h>

void bsp_beep_init(void);
void bsp_beep_on(void);
void bsp_beep_off(void);
void bsp_beep_toggle(void);
uint8_t bsp_beep_is_on(void);

#endif
