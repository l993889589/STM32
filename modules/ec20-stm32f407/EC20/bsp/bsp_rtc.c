#include "main.h"

#include "bsp_usart.h"

#include "rtc.h"

#include "bsp_rtc.h"
								 
#include "log.h"


time_t g_time_t={0};


void time_convet_sec(int* sec,time_t tt){
	
		*sec=tt.tm_hour*3600+tt.tm_min*60+tt.tm_sec;
	
}


void sec_convert_time(int sec,time_t *tt){
	
	if(sec>86400)sec-=86400;
	 tt->tm_hour=sec/3600;
   tt->tm_min=sec%3600/60;
   tt->tm_sec=sec%60;
	
}


void timToStamp(uint32_t *pStamp, time_t clock)
{
	static  int MON1[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};	//平年
	static  int MON2[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};	//闰年
	int *month = 0;
	uint32_t leapYearCnt = 0;
	uint32_t days = 0;
	//获得1970年到当前年的前一年共有多少闰天
	for(int i = 1970; i < clock.tm_year; i++)
	{
		if((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0))
		{
			leapYearCnt++;
		}
	}
	
	days = leapYearCnt * 366 + (clock.tm_year - 1970 -leapYearCnt) * 365;
	/*判断当前年是不是闰年*/
	if((clock.tm_year % 4 == 0 && clock.tm_year % 100 != 0) || (clock.tm_year % 400 == 0))
	{
		month = MON2;
	}
	else
	{
		month = MON1;
	}
	
	for(int i = 0; i < clock.tm_mon - 1; i++)
	{
		days += month[i];
	}
	
	*pStamp = (days + clock.tm_day-1) * 24 * 3600  + clock.tm_hour * 3600  + clock.tm_min * 60  + clock.tm_sec - 8 * 3600  ;//- 8 * 3600 * 1000
}





void bsp_set_alarm(time_t set_tt);



void bsp_get_rtc(time_t *set_tt)
{

	RTC_DateTypeDef sDate;
	RTC_TimeTypeDef sTime;
	HAL_RTC_GetTime(&hrtc,&sTime,RTC_FORMAT_BIN);	
	HAL_RTC_GetDate(&hrtc,&sDate,RTC_FORMAT_BIN);
	
	set_tt->tm_hour=sTime.Hours;
	set_tt->tm_min=sTime.Minutes;
	set_tt->tm_sec=sTime.Seconds;
	set_tt->tm_year=sDate.Year+2000;
	set_tt->tm_mon=sDate.Month;
	set_tt->tm_day=sDate.Date;
	
}	


uint32_t bsp_get_timetamp(){
	
	time_t set_tt={0};
	bsp_get_rtc(&set_tt);
	
	log("获取时间:[%d][%d][%d]-[%d][%d][%d]",set_tt.tm_year,set_tt.tm_mon,set_tt.tm_day,set_tt.tm_hour,set_tt.tm_min,set_tt.tm_sec);

	uint32_t timet=0;
	timToStamp(&timet,set_tt);
		
	return timet;
	
}


void SystemClock_Config(void);

void bsp_enter_stopmode(){
	
//	HAL_SuspendTick();//停止系统滴答计时器

//	CLEAR_BIT(SysTick->CTRL, SysTick_CTRL_ENABLE_Msk);//失能系统滴答定时器
	
	HAL_SuspendTick();//停止系统滴答计时器
	HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);//停机模式
	
//	SET_BIT(SysTick->CTRL, SysTick_CTRL_ENABLE_Msk);//使能系统滴答定时器

//	HAL_ResumeTick();//恢复系统滴答计时器
	
	SystemClock_Config();
	
	
}

//设置当前的RTC时间
void bsp_set_rtc(time_t set_tt){
	
	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef DateToUpdate = {0};
	
	sTime.Hours = set_tt.tm_hour;
  sTime.Minutes = set_tt.tm_min;
  sTime.Seconds = set_tt.tm_sec;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
		
    Error_Handler();
		
  }else{
			log("rtc set ok");
	}


  DateToUpdate.Date = set_tt.tm_day;
  DateToUpdate.Month = set_tt.tm_mon;
  DateToUpdate.Year = set_tt.tm_year-2000;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
}
