//时基
//回调
//数据输入
#include "liqueue.h"
#include "usart.h"

#include "debug_config.h"

#include "bsp_usart.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>	

#define DEBUG_NODE_SIZE		  		 2
#define DEBUG_PACKET_SIZE  			 256
#define DEBUG_MEM_POOL_SIZE		   256*2



lq_handle debug_queue = {0};

uint8_t debug_mem_pool[DEBUG_MEM_POOL_SIZE]={0};
uint8_t debug_node_pool[NODE_ARRAY_SIZE(DEBUG_NODE_SIZE)];

void debug_serialport_init(){
	
		int ret = lq_mem_array_init(&debug_queue, debug_mem_pool, DEBUG_MEM_POOL_SIZE, debug_node_pool,DEBUG_NODE_SIZE);

		if(ret){
		
			//debug_printf("debug_mem_init ok,ret=[%d]\r\n",ret);
		}else{
			
			//debug_printf("debug_mem_init error,ret=[%d]\r\n",ret);
		}
	
		lq_timer_init(&debug_queue,30);
	
	
		ret = lq_init(&debug_queue,DEBUG_PACKET_SIZE, NO_OVERWRITE);
	
}

void debug_serialport_time_tick()
{
	  /*与时间基准相关，systick->1*/
    lq_tick(&debug_queue,1);  
}


void debug_serialport_msg_process()
{
    uint8_t pop_buff[DEBUG_PACKET_SIZE + 1]={0};

		size_t readcnt = lq_pop(&debug_queue, pop_buff, DEBUG_PACKET_SIZE + 1);

		if (readcnt)
		{
				debug_message_handler(pop_buff, readcnt);
		}
    
}





void wait_task(){

    //thread_sleep(5);
		//喂狗
}

int debug_block_read(uint8_t * buff,int buffsize,int timeout)
{
   return lq_read_ex(&debug_queue,buff,buffsize,timeout,wait_task);
}




void debug_message_in(unsigned char *buf)
{
   lq_add(&debug_queue,buf, 1);
}











