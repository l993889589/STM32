#ifndef __BSP_RTC_H_
#define __BSP_RTC_H_

#include "main.h"

typedef struct{

    int  tm_sec;
    int  tm_min;
    int  tm_hour;

    int  tm_day;
    int  tm_mon;
    int  tm_year;

}time_t;

void bsp_set_alarm(time_t set_tt);

void bsp_enter_stopmode(void);

void bsp_set_after_wake(int after_sec);

uint32_t bsp_get_timetamp(void);

void bsp_set_rtc(time_t set_tt);
#endif 