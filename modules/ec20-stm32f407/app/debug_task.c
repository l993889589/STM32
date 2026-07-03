#include "bsp_usart.h"
#include "debug_config.h"
#include "app_cmd.h"
#include "rs485_config.h"
#include "log.h"

void debug_message_handler(unsigned char *message, int messagelen)
{

	debug_printf("main recv:%d,[%s]\r\n",messagelen,message);
//	debug_printf("main recv:%d,[%s]\r\n",messagelen,message);


	app_at_process(message,messagelen, USART_AT_IN);
//	
//	bsp_ec20_write(message,messagelen);
}


