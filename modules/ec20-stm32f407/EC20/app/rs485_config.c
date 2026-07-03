#include "main.h"
#include "bsp_usart.h"
#include "rs485_config.h"
#include "usart.h"
#include "bsp_usart.h"
#include "liqueue.h"
#include "string.h"




lq_handle rs485_queue={0};

#define RS485_NODE_SIZE		  		 2
#define RS485_PACKET_SIZE  			 128
#define RS485_MEM_POOL_SIZE		   128*2

uint8_t rs485_mem_pool[RS485_MEM_POOL_SIZE]={0};
uint8_t rs485_node_pool[NODE_ARRAY_SIZE(RS485_NODE_SIZE)];


void rs485_serialport_init()
{
	
		size_t ret =lq_mem_array_init(&rs485_queue, rs485_mem_pool, RS485_MEM_POOL_SIZE, rs485_node_pool,RS485_NODE_SIZE);
	
		if(ret){
			
//				debug_printf("485_mem_init ok\r\n");
		}else{
//				debug_printf("485_mem_init error");
		}
	
		ret = lq_init(&rs485_queue,RS485_PACKET_SIZE, NO_OVERWRITE);
	
		lq_timer_init(&rs485_queue,30);
		
		if(ret){
				//debug_printf("lq_init ok\r\n");
		}else{
				//debug_printf("lq_init error");
		}

		
}

void rs485_serialport_timer_tick()
{
			lq_tick(&rs485_queue,1);
}

void rs485_serialport_process(){

    uint8_t pop_buff[RS485_PACKET_SIZE+1];

		memset(pop_buff,0,RS485_PACKET_SIZE+1);
	
		size_t readcnt = lq_pop(&rs485_queue, pop_buff, RS485_PACKET_SIZE+1);

	
		if(readcnt){

					rs485_message_handler(pop_buff,readcnt);
			
		}else{
					
		}

}


void rs485_message_in(unsigned char *buf, unsigned int len)
{

    lq_add(&rs485_queue,buf, len);
}


void rs485_wait_task(){

		
		//debug_printf("[%d][%d][%d]",module_uart_queue.mem.node_list.block.timetick,module_uart_queue.mem.node_list.block.index,module_uart_queue.timer.tick);
    //thread_sleep(5);
		//Î¹¹·¡¢
}


uint8_t rs485_block_read(uint8_t *buff,uint32_t size,uint32_t timeout){
	

		return lq_read(&rs485_queue, buff ,size,timeout);

}


void rs485_baud_set(uint32_t baud){


	huart6.Instance = USART2;
  huart6.Init.BaudRate = baud;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }


}





















