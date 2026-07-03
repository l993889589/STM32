#include "timer_task.h"



void timer_task_reg(timer_handle *h,uint32_t timefre){
	
		h->state=0;
		h->tick=timefre;
		h->set=timefre;
		h->call=0;
	
}

void timer_task_tick(timer_handle *h){
	
		if(h->state){
			
			h->tick--;
			if(h->tick==0){
				h->tick=h->set;
				h->call=1;
			}
			
		}
}


void timer_task_set(timer_handle *h,uint32_t timeset){
//		if(!h)return;
//		h->set = timeset;
//		h->tick= timeset;
//		h->state=0;
		timer_task_reg(h,timeset);
}




void timer_task_suspend(timer_handle *h,uint8_t state){
		if(state)
		h->state=0;
		else h->state=1;
}


uint8_t timer_task_iscall(timer_handle *h){
	
		if(h->call){
			h->call=0;
			return 1;
		}
		return 0;
}

