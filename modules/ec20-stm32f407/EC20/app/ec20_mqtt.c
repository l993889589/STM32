#include "ec20_mqtt.h"
#include "log.h"
#include "ec20_lib.h"
#include "ec20_drv.h"
#include "main.h"
#include "ec20_network.h"
#include "app_pub_onenet.h"
#include "stdio.h"
#include "app_flashlite.h"
#include "app_flashid.h"
#include "string.h"

#include "app_server.h"

#include "cJSON.h"

ec20_mqtt_handle g_ec20_mqtt={0};


char *topic1="/sys/hcdveS55gpY/ITD1/thing/service/property/set";

void mqtt_msgcb(uint8_t* topic,uint8_t *data,int len){
	
	log("[%s]:%s",topic,data);
	
	cmd_process((char *)data);
	// json的基础你总会了吧。
	//总是一个key 和一个 value
	//value 有几个类型，string ，浮点 ，number ，bool ，obj ，数组
	//那么这个obj是啥，就是他的value 如果是一个json大括号，那他就是obj，也就是下一层


//{
//	"method": "thing.service.property.set", //string
//	"id": "1627877991", //string
//	"params": { //obj params的value 是新的一个json
//		"relay": 1
//	},
//	"version": "1.0.0" //string
//}





}



void mqtt_concb(uint8_t state){
	
	if(state){
			
		log("channel_1链接mqtt服务器成功");
	
		if(ec20_app_mqtt_sub_onenet()){

			/*发送设备属性*/
//			app_pub_device_info();
//			log("发送设备信息");	

		}
				
	}else{
				
		log("channel_1已断开mqtt连接");
			
	}

}



uint8_t ec20_app_mqtt_sub_onenet(void)
{
	/*订阅设备属性上报响应*/
	if(ec20_at_mqtt_sub(1,(uint8_t  *)"$sys/wCmZ5S4hid/ITD_1/thing/property/post/reply",0)){
	
		log("已订阅设备属性上报topic");
	
	}
	/*订阅设备属性设置请求*/
	if(ec20_at_mqtt_sub(1,(uint8_t  *)"$sys/wCmZ5S4hid/ITD_1/thing/property/set",0)){
	
		
		log("已订阅设备属性设置请求topic");
	}


}



void ec20_app_mqtt_init(void)
{

		ec20_mqtt_param param={0};
		
//		app_flash_read(ONENET_ADDRESS,(uint8_t*)param.ip,MQTT_MON_IP_MAX);
//		app_flash_read(ONENET_PORT,(uint8_t*)&param.port,4);
//		app_flash_read(ONENET_CLIENTID,(uint8_t*)param.clientid,MQTT_MON_CID_MAX);
//		app_flash_read(ONENET_USERNAME,(uint8_t*)param.username,MQTT_MON_UNAME_MAX);
//			app_flash_read(ONENET_PASSWORD,(uint8_t*)param.password,MQTT_MON_PSD_MAX);
//		
//		log("开始读取mqtt参数");
//		log("ip:%s",param.ip);
//		log("port:%d",param.port);
//		log("clientid:%s",(uint8_t*)param.clientid);
//		log("username:%s",(uint8_t*)param.username);
//		
		ec20_lib_mqtt_regcb(&g_ec20_mqtt,mqtt_msgcb,mqtt_concb);
		
		if(ec20_at_connect_onenet_version_config(1,4))
		{
			log("发送MQTT协议版本号");
		}
//		
		if(ec20_at_recv_len_enable(1,1)){
		
			log("使能接收长度");
		}
		/*onenet新平台*/
		//ec20_mqtt_init(&g_mqtt_handle,1,(uint8_t *)"mqtts.heclouds.com",1883,(uint8_t *)"ITD_1",(uint8_t *)"wCmZ5S4hid",(uint8_t *)"version=2018-10-31&res=products%2FwCmZ5S4hid%2Fdevices%2FITD_1&et=1716441208&method=md5&sign=b94RP0G5OeJ%2FE06a%2FQokGQ%3D%3D");
		/*onenet多协议接入*/
		//ec20_mqtt_init(&g_mqtt_handle,1,param.ip,param.port,param.clientid,param.username,param.password);
		
		//ec20_lib_mqtt_init(&g_ec20_mqtt_channel_1,1,(uint8_t *)"183.230.40.39",6002,(uint8_t *)"1017978411",(uint8_t *)"554843",(uint8_t *)"linhao");
		
		/*aliyun*/
		 //ec20_at_connect_aliauth_config(mqtt_handle->param.channel,mqtt_handle->param.clientid,(char*)mqtt_handle->param.username,(char*)mqtt_handle->param.password);
		
		ec20_lib_mqtt_init(&g_ec20_mqtt,1,(uint8_t *)"iot-06z00id3r78q2ds.mqtt.iothub.aliyuncs.com",1883,(uint8_t *)"hcdveS55gpY",(uint8_t *)"ITD1",(uint8_t *)"d840c5d0270d4cc5cb9d80292c2a56ef");
		
		
		ec20_app_mqtt_control(1);
}







void ec20_app_mqtt_tick(void){

		if(ec20_app_network_is_call()){
		
				ec20_lib_mqtt_tick(&g_ec20_mqtt,1);
		
		}
}



void ec20_app_mqtt_process(void){

		if(ec20_app_network_is_call()){
		
				ec20_lib_mqtt_process_aliauth(&g_ec20_mqtt);
				//ec20_lib_mqtt_process(&g_ec20_mqtt);
		}
}





uint8_t ec20_app_mqtt_yeild(char *msg, int length){

	if(ec20_lib_mqtt_yeild(&g_ec20_mqtt,msg, length)){
	
			return 1;
	}
	
	return 0;
}

uint8_t ec20_app_mqtt_pub(uint8_t channel,uint8_t * topic,uint8_t *msg,int length)
{
	ec20_at_mqtt_pub(channel,topic,msg,length);
}


//关闭不能放到事件里，会阻塞住
uint8_t ec20_app_mqtt_control(uint8_t state){
	

	
	if(state){
		ec20_lib_mqtt_control(&g_ec20_mqtt,state);
		
	}else{
#if(0)
		
			xSemaphoreTake(g_mqpub_semaphore , portMAX_DELAY);	
			ec20_mqtt_close_wait(&g_mqtt_handle,1);
		  xSemaphoreGive(g_mqpub_semaphore );
#else
		
			ec20_lib_mqtt_control(&g_ec20_mqtt,state);
#endif
		
	}
			
	return 1;
}



