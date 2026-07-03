#include "app_gather.h"

#include "rs485_config.h"

#include "log.h"

#include "app_info.h"

#include "string.h"
#include "stdio.h"

#include "timer_task.h"

#include "leduo.h"

timer_handle gather_call={0};


uint8_t app_gather_init(void)
{
	timer_task_reg(& gather_call,10000);
	timer_task_suspend(& gather_call,0);
	return 1 ;
}


void app_gather_tick(void)
{

	timer_task_tick(&gather_call);
}


void app_gather_process(void)
{

		if( timer_task_iscall(&gather_call))
		{
//			app_querry_device_JY_DAM();
		}
	
}



sensor_sht30 s_sht30 ={0};

uint8_t querry_sht30_device_JY_WS1(sensor_sht30 *s_sht30 )
{

		uint8_t buff[256]={0};
		//开始推送
		uint8_t msg[]={0xFE,0x04,0x00,0x00,0x00,0x02,0x65,0xC4};//温湿度
		
		
		
		log_hex_msg("write",msg,sizeof(msg));
		
		rs485_write(msg,sizeof(msg));
		
		int ret= rs485_block_read(buff,256,3000);
		
		log("ret=%d",ret);
		
		log_hex_msg("recv",buff,ret);
		
		if(ret==9){
		
				if(buff[0]==0xFE&&buff[1]==0x04&&buff[2]==0x04){
				
							
							float temp=0;
							float humi=0;
							
							uint8_t data[4]={0};
							
							data[0]=buff[3];
							data[1]=buff[4];
							
							data[2]=buff[5];
							data[3]=buff[6];
							
							temp=(float)((data[0]<<8)+data[2])/100;
							
							humi=(float)((data[2]<<8)+data[3])/100;
							
							log("temp=%.2f,humi=%.2f",temp,humi);
							
							s_sht30->temp=temp;
							
							s_sht30->humi=humi;
							
							return 1;
							
				}	

		}
		
		return 0;
}


void app_querry_sht30()
{
	querry_sht30_device_JY_WS1(&s_sht30);
}


uint16_t crc_cal_value(unsigned char *data_value, unsigned char data_length)
{
		int i;
		uint16_t crc_value = 0xffff;
		while (data_length--)
		{
			crc_value ^= *data_value++;
			for (i = 0; i < 8; i++)
			{
				if (crc_value & 0x0001)
						crc_value = (crc_value >> 1) ^ 0xA001;
				else
						crc_value = crc_value >> 1;
			}
		}
		return (crc_value);
}



/*55 aa 666 66 6 6 6*/



uint8_t control_device_jy_dam_relay(uint8_t addr,uint8_t channel,uint8_t state){

		
		uint8_t buff[20]={0};
		uint16_t crc;
		
//		uint8_t msg[20]={addr,0x05,0x00,channel-1,~state,0x00};
		
		uint8_t msg[20]={0x12,0x05 ,0x00 ,0x00, 0xFF, 0x00 ,0x8E ,0x99 };
		
//		crc= crc_cal_value(msg, 6);
//		
//		memcpy(msg+6,&crc,2);
//		
		rs485_write(msg,8);
		
		int ret= rs485_block_read(buff,256,1000);
		
		if(ret<4){
		
				log("设备相应错误");
				return 0;
			
		}
		
		log_hex_msg("recv",buff,ret);
		
		if(buff[0]==addr &&buff[1]==0x05){
		
				return 1;
				
		}
		
		
		
	
}




















