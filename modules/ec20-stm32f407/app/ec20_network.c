#include "main.h"
#include "log.h"
#include "ec20_lib.h"
#include "ec20_drv.h"

#include "stdarg.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"

#include "bsp_rtc.h"
#include "ec20_network.h"
#include "ec20_mqtt.h"



ec20_network g_ec20_network={0};


void at_callback(uint8_t state){

		if(state){
			
				log("检测到4G模组");
			
				if(ec20_at_version_query()){
			
						log("模组型号:EC20");
				}
					
		}else{
		
			log("检测不到4G模组");
			
		}
}


void sim_callback(uint8_t state){

		if(state){
				
				log("检测SIM卡");
				
			
		}else{
			
			log("SIM卡失效");
			
		}
}

void reg_callback(uint8_t state){

		if(state){
				
				log("已经驻网");
			
		}else{
			
			log("驻网失败");
			
		}
}

void call_callback(ec20_time * time){

		log("数据拨号成功:%d-%d-%d:%d-%d-%d",time->year,time->month,time->day,time->hour,time->minute,time->second);
			
		time_t set_tt={0};
		set_tt.tm_year=time->year;
		set_tt.tm_mon=time->month;
		set_tt.tm_day=time->day;
		set_tt.tm_hour=time->hour;
		set_tt.tm_min=time->minute;
		set_tt.tm_sec=time->second;
		
		bsp_set_rtc(set_tt);
		
		ec20_app_mqtt_init();

}



void ec20_app_network_tick(void){

		ec20_lib_network_tick(&g_ec20_network,1);
		
}


void ec20_app_network_init(void){
	
		ec20_lib_network_init(&g_ec20_network);
		ec20_lib_network_event_add(&g_ec20_network,at_callback,sim_callback,reg_callback,call_callback);
		
}

void ec20_app_network_process(void){

		ec20_lib_network_process(&g_ec20_network);
		
}


uint8_t ec20_app_network_is_call(void){

		ec20_lib_network_get_call_state(&g_ec20_network);
		
}

device_imei_info ec20_app_network_get_imei(void){

		return g_ec20_network.imei;
}

uint8_t ec20_app_network_get_imei_ex(device_imei_info	*imei){

		return ec20_lib_network_get_imei(&g_ec20_network,imei);

}


device_iccid_info ec20_app_network_get_iccid(void){
			
		return g_ec20_network.iccid;
		
}



