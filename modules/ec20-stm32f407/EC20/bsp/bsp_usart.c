#include "usart.h"
#include "bsp_usart.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>	
#include <string.h>

#include "app_flashlite.h"
#include "app_flashid.h"

#define DEBUG_TXBUFF_SIZE 	(512)
#define MODULE_TXBUFF_SIZE 	(512)
#define RS485_TXBUFF_SIZE 	(512)

log_state s_log = {0};

void bsp_usart_init()
{
	
	/*debug*/
	debug_log_init();
	__HAL_UART_ENABLE_IT(&huart1, UART_IT_PE);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);	
	/*module*/
  __HAL_UART_ENABLE_IT(&huart3, UART_IT_PE);
  __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);	
	/*rs485*/
  __HAL_UART_ENABLE_IT(&huart6, UART_IT_PE);
  __HAL_UART_ENABLE_IT(&huart6, UART_IT_RXNE);
		
}


uint8_t log_control(log_state *log_state,uint8_t state)
{
	log_state->log_flag=state;
		return 1;
}

uint8_t app_log_control(uint8_t state)
{
	log_control(&s_log,state);
	
	uint32_t en=state;
	
	if(app_flash_write(LOG_EN,(void*)&en,4)){
	
	}
	
	return 1;
}

void debug_log_init(){

		uint32_t en=0;
    if(app_flash_read(LOG_EN,(void*)&en,4)){
		
			s_log.log_flag=en;
		
		}else{
		
			s_log.log_flag=0;
		}
}




int debug_printf(char *format, ...)
{
		if(s_log.log_flag)
		{
				va_list args;
				unsigned int length;

				unsigned char printf_txbuf[DEBUG_TXBUFF_SIZE]={0};

				/*发送长度过大时需调整缓存大小*/

				va_start(args, format);
				length = vsnprintf((char *)printf_txbuf, DEBUG_TXBUFF_SIZE, (char *)format, args);
				va_end(args);

				HAL_UART_Transmit(&huart1,printf_txbuf, length, 0xffff);

				return 1;
		}

}


void debug_write(unsigned char *data, int length)
{
		HAL_UART_Transmit(&huart1,data, length, 0xffff);
}


int bsp_ec20_printf(char *format, ...)
{

    va_list args;
    unsigned int length;

    unsigned char printf_txbuf[MODULE_TXBUFF_SIZE];
    
    va_start(args, format);
    length = vsnprintf((char *)printf_txbuf, MODULE_TXBUFF_SIZE, (char *)format, args);
    va_end(args);

		HAL_UART_Transmit(&huart3,printf_txbuf, length, 0xff);

    return 1;
}

void bsp_ec20_write(unsigned char *data, int length)
{
	
	
		HAL_UART_Transmit(&huart3,data, length, 0xff);
	
}

int rs485_printf(char *format, ...)
{

    va_list args;
    unsigned int length;

    unsigned char printf_txbuf[ RS485_TXBUFF_SIZE];

    va_start(args, format);
    length = vsnprintf((char *)printf_txbuf,  RS485_TXBUFF_SIZE, (char *)format, args);
    va_end(args);

		HAL_UART_Transmit(&huart6,printf_txbuf, length, 0xff);

    return 1;
}

void rs485_write(unsigned char *data, int length)
{
		rs485_tx_enable(1);
	
		HAL_UART_Transmit(&huart6,data, length, 0xff);
		
		rs485_tx_enable(0);
}


void rs485_tx_enable(uint8_t state)
{
	if(state){
	
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_SET);
	}else{
	
			HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_RESET);
	}
}

//uint8_t set_usart_baud(UART_HandleTypeDef *huart, uint32_t BaudRate){

//	 //关闭串口
//  HAL_UART_DeInit(huart);
//  //修改波特率
//  huart->Init.BaudRate = BaudRate;
//  //重新初始化串口
//  HAL_UART_Init(huart);	
//	
//	return 1;
//}


//uint8_t  rs485_set_baud(uint32_t BaudRate){

//	set_usart_baud(&huart3, BaudRate);
//	return 1;
//}

