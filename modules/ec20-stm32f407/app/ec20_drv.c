

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>	
#include "ec20_drv.h"
#include "debug_config.h"
#include "module_config.h"
#include "bsp_usart.h"
#include "string.h"

#include "log.h"


//调试使能宏定义
#define MODULE_TXBUFF_SIZE 1024/**/



int ec20_drv_write(uint8_t *buff,int len){
	
//		if(len<120){
//			 log("moduwrite:[%s]",buff);
//		 }else{
//			 
//			 log("moduwrite:[%.*s....略]",120,buff);
//		 }

		log("moduwrite:[%s]",buff);
	  bsp_ec20_write(buff,len);

		 return 1;

}


uint32_t ec20_drv_printf(char *format, ...){
	
    va_list args;
    unsigned int length;

    unsigned char printf_txbuf[MODULE_TXBUFF_SIZE]={0};

    /*发送长度过大时需调整缓存大小*/

    va_start(args, format);
    length = vsnprintf((char *)printf_txbuf, MODULE_TXBUFF_SIZE, (char *)format, args);
    va_end(args);

		ec20_drv_write(printf_txbuf, length);

    return 1;	
	
}


void ec20_drv_writestr(char *data){
	
		 ec20_drv_write((uint8_t*)data,strlen(data));

}


int ec20_drv_blockread(uint8_t *buff,uint32_t size,uint32_t timeout){
	
		memset(buff, 0, size);

		int ret= module_block_read(buff,size,timeout);
	
		if(ret == 0){
				
				log("block read timeout");
				
				return  0;
								
		}else{
			
			log("block ret:%d,[%s]", ret,buff);	
			
			return ret;
		}
		
}




uint32_t drv_power_on(void)
{

}
uint32_t drv_power_off(void)
{

}


































