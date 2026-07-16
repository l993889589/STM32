#ifndef _DWIN_APP_H_
#define _DWIN_APP_H_

#include "stdint.h"


#define REG_WARNING_ADDR1 0X1320
#define REG_WARNING_ADDR2	0X1330
#define REG_WARNING_ADDR3	0X1340
#define REG_WARNING_ADDR4	0X1350
#define REG_WARNING_ADDR5	0X1360
#define REG_WARNING_ADDR6	0X1370

uint8_t dwin_set_page(uint8_t page);
uint8_t dwin_set_pwm(uint8_t duty);


#endif









