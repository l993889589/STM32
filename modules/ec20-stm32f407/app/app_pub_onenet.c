#include "app_pub_onenet.h"
#include "log.h"
#include "main.h"
#include "ec20_mqtt.h"
#include "ec20_network.h"
#include "ec20_lib.h"
#include "stdio.h"
#include "app_dht11.h"
#include "string.h"
#include "stdlib.h"
#include "timer_task.h"
/*发送Onenet数据流*/


//AT+QMTPUBEX=0,0,0,0,"$sys/6dDOIg7TMQ/EC20-Test/thing/property/post",92



//{"id":"123","version":"1.0","params":{"step":{"value":15},"BatteryPercentage":{"value":56}}}



timer_handle uplpod_call={0};

uint8_t app_upload_init(void)
{
	timer_task_reg(& uplpod_call,60000);
	timer_task_suspend(& uplpod_call,0);
}

void app_upload_tick(void)
{

	timer_task_tick(&uplpod_call);
}


void app_upload_process(void)
{
		if(ec20_app_network_is_call()){
		
				if( timer_task_iscall(&uplpod_call))
				{
//					app_pub_device_anaolg();
				}
			
		}
	
}





uint8_t app_pub_device_info(void)
{
	
	uint8_t buff[1024]={0};
	
	device_imei_info imei={0};
	
//	uint8_t imei=ec20_app_network_get_imei();
	

	
//	int ret =sprintf((char*)buff,"{\"id\": \"123\",\"version\": \"1.0\",\"params\": {\"AlarmState\": {\"value\": true},\"imei\": {\"value\": %s}}}",imei.imei);
	int ret =sprintf((char*)buff,"{\"id\": \"123\",\"version\": \"1.0\",\"params\": {\"relay1\": {\"value\": true},\"relay2\": {\"value\": true},\"relay3\": {\"value\": true},\"relay4\": {\"value\": true},\"imei\": {\"value\": \"%s\"}}}",imei.imei);

	if(ret){
	
//			ec20_app_mqtt_pub(1,(uint8_t *)"$sys/wCmZ5S4hid/ITD_1/thing/property/post",(uint8_t *)buff,ret);
	}
	
}


uint8_t app_pub_device_anaolg(void){
		
	uint8_t buff[1024]={0};
	
	uint8_t  temp=dht11_data_access().temperature;
		
	uint8_t  humi=dht11_data_access().humidity;	
		
	int ret =sprintf((char*)buff,"{\"id\": \"123\",\"version\": \"1.0\",\"params\": {\"temp\": {\"value\": %d},\"humi\": {\"value\": %d}}}",temp,humi);
	
	if(ret){
	
//			ec20_app_mqtt_pub(1,(uint8_t *)"$sys/wCmZ5S4hid/ITD_1/thing/property/post",(uint8_t *)buff,ret);
	}
}

