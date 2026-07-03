#include "main.h"

typedef struct{
	
	uint32_t tick;
	uint32_t set;
	volatile uint8_t call;
	uint8_t	state;
	
}timer_handle;

void timer_task_reg(timer_handle *h,uint32_t timefre);
void timer_task_tick(timer_handle *h);
void timer_task_suspend(timer_handle *h,uint8_t state);
uint8_t timer_task_iscall(timer_handle *h);
void timer_task_set(timer_handle *h,uint32_t timeset);
