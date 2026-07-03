#include "bsp_usart.h"
#include "module_config.h"
#include "ec20_mqtt.h"
#include "log.h"

void module_message_handler(unsigned char *message, int messagelen)
{

//		debug_printf("recv:%s\r\n",message);
		if(ec20_app_mqtt_yeild((char*)message,messagelen)){
			
			log("mqtt_yeile success");
			
		}else{
			
			debug_printf("recv:%s\r\n",message);
		}

}