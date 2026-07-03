#include "liqueue.h"
#include "usart.h"
#include "module_config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>	
#include "bsp_usart.h"

#define MODULE_NODE_SIZE		  		 2
#define MODULE_PACKET_SIZE  			 512
#define MODULE_MEM_POOL_SIZE		   512*2


uint8_t module_mem_pool[MODULE_MEM_POOL_SIZE]={0};
uint8_t module_node_pool[NODE_ARRAY_SIZE(MODULE_NODE_SIZE)];

lq_handle module_queue = {0};


void module_serialport_init(){
	
		int ret = lq_mem_array_init(&module_queue, module_mem_pool, MODULE_MEM_POOL_SIZE, module_node_pool,MODULE_NODE_SIZE);

			if(ret){
		
			//debug_printf("lq_mem_modue_init ok\r\n");
				
		}else{
			
			//debug_printf("lq_mem_module_init error\r\n");
		}
	
		lq_timer_init(&module_queue,30);
	
		ret = lq_init(&module_queue,MODULE_PACKET_SIZE, NO_OVERWRITE);
	
}

void module_serialport_time_tick()
{
    lq_tick(&module_queue,1);
}


void module_message_in(unsigned char *buf)
{
   lq_add(&module_queue,buf, 1);
}

void module_msg_process()
{
    uint8_t pop_buff[MODULE_PACKET_SIZE + 1]={0};

		size_t readcnt = lq_pop(&module_queue, pop_buff, MODULE_PACKET_SIZE + 1);

		if (readcnt)
		{
				module_message_handler(pop_buff, readcnt);
		}else{
			
		}  
}

void module_wait_task(){

		
		//debug_printf("[%d][%d][%d]",module_uart_queue.mem.node_list.block.timetick,module_uart_queue.mem.node_list.block.index,module_uart_queue.timer.tick);
    //thread_sleep(5);
		//喂狗
}

int module_block_read(uint8_t * buff,int buffsize,int timeout)
{

   lq_read_ex(&module_queue,buff,buffsize,timeout,module_wait_task);
	
}












void module_uart_write(unsigned char* data,int len)
{
    HAL_UART_Transmit(&huart1,data, len, 0xFF);
}
