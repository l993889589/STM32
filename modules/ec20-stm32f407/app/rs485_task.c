#include "bsp_usart.h"

#include "rs485_config.h"
#include "log.h"


void rs485_message_handler(unsigned char *message, int messagelen)
{
		log("rs485 recv:%s",message);
}
