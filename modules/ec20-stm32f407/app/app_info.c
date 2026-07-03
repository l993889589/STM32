#include "app_info.h"

#include "main.h"
#include "log.h"

#include "app_flashlite.h"
#include "app_flashid.h"

#include "bsp_rtc.h"

#include "ec20_network.h"
#include "ec20_lib.h"

#include "stdio.h"
#include "string.h"

#include "rs485_config.h"


//推送数据的实现
#define  START_ADD  (0XE000+0x08000000+0x10000)//0XC800+0x08000000+0x10000

void print_packet_size(){
	
	log("packet size:%d",sizeof(packet_info));
}


packet_info_flash_handle g_packet_handle={0};




#define MAX_SAVE_SIZE 1440//1440



void packet_save_info(){
	
	packet_info_flash_handle packet={0};
	
	memcpy(&packet,&g_packet_handle,sizeof(packet_info_flash_handle));
	
	log("更新了缓存信息>>>>>>");
	//写入
	app_flash_write(PACKET_INFO,(void*)&packet,sizeof(packet_info_flash_handle));

}



int packet_get_use_size(){
	

	if(g_packet_handle.head >= g_packet_handle.tail){
		
		return g_packet_handle.head-g_packet_handle.tail;
		
	}else{
		
		return (MAX_SAVE_SIZE)-(g_packet_handle.tail -g_packet_handle.head );
	}
	
}


#define TEST_CMT 1500

void packet_data_test(){
	
	
	 packet_info_clear();	
	
	 log("开始写入测试****************");
	
	 for(int i=0;i<TEST_CMT;i++){
		 
		 packet_info _info={0};
		 
		 _info.s_airheight.high=i;
		 _info.s_airheight.roll=-i;
		 _info.s_airheight.vertical=i*2;
		 
			_info.s_airheight.state|=1<<0;
			_info.s_airheight.state|=1<<1;
			_info.s_airheight.state|=1<<2; 
		 
		 _info.state|=1;
		 
		 
		 //log("模拟填充了数据");
		 packet_info_add(&_info);
	 }
	 
	 
	 log("开始读取测试****************************************************************\r\n\r\n");
	 
	 for(int i=0;i<TEST_CMT;i++){
		 
		 packet_info _info={0};
 
		 if(packet_info_read_old(&_info)){
			 
			 //log("读取:[%f][%f][%f]",_info.s_airheight.high,_info.s_airheight.roll,_info.s_airheight.vertical);
			 
			 packet_info_next();
		 }
		 
		 
	 }	 
	 
	 
	
}



void packet_info_clear(){
	
		log("已清空离线缓存");
	
		g_packet_handle.head=0;
	  g_packet_handle.tail=0;
		g_packet_handle.max_set = MAX_SAVE_SIZE;
	
	 //存储以下当前的节点，
	 app_flash_write(PACKET_INFO,(void*)&g_packet_handle,sizeof(packet_info_flash_handle));		
	
}

void packet_info_save_info(){
	
	//存储以下当前的节点，
	app_flash_write(PACKET_INFO,(void*)&g_packet_handle,sizeof(packet_info_flash_handle));
}




void packet_info_next(){
	
	if(g_packet_handle.tail == g_packet_handle.head){
		return;
	}
	
	g_packet_handle.tail=(g_packet_handle.tail+1)% (MAX_SAVE_SIZE);
	

}


void packet_info_init(){


	
	if(app_flash_read(PACKET_INFO,(void*)&g_packet_handle,sizeof(packet_info_flash_handle))){
		
		g_packet_handle.max_set = MAX_SAVE_SIZE ;
		
	}else{
		
		g_packet_handle.head=0;
		g_packet_handle.tail=0;
		g_packet_handle.max_set = MAX_SAVE_SIZE ;
	}
	log("flash最大地址:0x0803FFFF");
	if(START_ADD+PACKET_INFO_SIZE*MAX_SAVE_SIZE<0x0803FFFF){
		log("数据库设置化正常");
		log("数据库初始化，起始地址:[%#x],结束地址:[%#x],单个数据大小:[%d byte],最大支持数据:[%d],总容量:[%d kb]",START_ADD,START_ADD+PACKET_INFO_SIZE*MAX_SAVE_SIZE,PACKET_INFO_SIZE,MAX_SAVE_SIZE,PACKET_INFO_SIZE*MAX_SAVE_SIZE/1024);
	}
	
}



//有一个格子拿来装东西拉。
void packet_info_add(packet_info *_info){

	
			

		static int tick=0;
		//计算要写入的位置
		uint32_t write_add= START_ADD + PACKET_INFO_SIZE * g_packet_handle.head;
	
	
	  log("写入:offset[%d],地址:[%#x],时间戳:[%u],大小:[%d] 使用量:%d/%d",g_packet_handle.head,write_add, _info->time_samp,PACKET_INFO_SIZE,packet_get_use_size(),MAX_SAVE_SIZE);
	
//	  log("vbat [%.3f][%.3f]",_info->d_info.csq,_info->d_info.vbat);
//		log("airheight [%.3f][%.3f][%.3f][%.3f][%d]",_info->s_airheight.high,_info->s_airheight.roll,_info->s_airheight.vertical,_info->s_airheight.temp,_info->s_airheight.state);
//		log("conducti  [%.3f][%.3f][%.3f][%.3f][%d]",_info->s_conductivity.ddl,_info->s_conductivity.tds,_info->s_conductivity.temp,_info->s_conductivity.yd,_info->s_conductivity.state);
//		log("flowmeter [%.3f][%.3f][%.3f][%.3f][%.3f][%.3f][%d]",_info->s_flowmeter.deep,_info->s_flowmeter.ll,_info->s_flowmeter.roll ,_info->s_flowmeter.speed ,_info->s_flowmeter.temp,_info->s_flowmeter.vertical,_info->s_flowmeter.state);
//		log("conducti  [%.3f][%.3f][%d]",_info->s_watergage.high ,_info->s_watergage.temp ,_info->s_watergage.state);		

		//写入
	  W25QXX_Write((void*)_info,write_add,PACKET_INFO_SIZE);
		
		packet_info r_info={0};
		
//		log("校验写入");
//		bsp_flash_read(write_add,(void*)&r_info,PACKET_INFO_SIZE);
//		
//		int cret = buff_cmp((void*)&r_info,(void*)_info,PACKET_INFO_SIZE);
//		
//		if(cret)
//		log("校验通过~");
//		else log("校验不通过!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

		g_packet_handle.head =(g_packet_handle.head +1) % (MAX_SAVE_SIZE);
	
	  log("新head:[%d]",g_packet_handle.head);
		
		if(g_packet_handle.head == g_packet_handle.tail ){
			
			g_packet_handle.tail=(g_packet_handle.tail + 1) % (MAX_SAVE_SIZE);
			log("覆盖了一个节点，新的tail:%d",g_packet_handle.tail);
		}
		
		
		
//		tick++;
//		
//		if(tick>=10){//点积累到一定程度才写进去
//			tick=0;
//			
//			packet_save_info();
//			
//		}

}


//只读取最早的数据，不会挪动指针,需要next手动移位，
uint8_t packet_info_read_old(packet_info *_info){
	
		
	
		if(g_packet_handle.head != g_packet_handle.tail){
			
			uint32_t read_add= START_ADD + PACKET_INFO_SIZE * g_packet_handle.tail;
			
			log("读取节点:offset [%d] 地址:[%#x],大小:[%d]当前剩余:%d/%d", g_packet_handle.tail ,read_add  ,PACKET_INFO_SIZE  ,packet_get_use_size()  ,MAX_SAVE_SIZE);	

			W25QXX_Read((void*)_info,read_add ,PACKET_INFO_SIZE);
//			
//			log("节点内容 [%d]->",g_packet_handle.tail);
//			log("vbat [%.3f][%.3f]",_info->d_info.csq,_info->d_info.vbat);
//			log("airheight [%.3f][%.3f][%.3f][%.3f][%d]",_info->s_airheight.high,_info->s_airheight.roll,_info->s_airheight.vertical,_info->s_airheight.temp,_info->s_airheight.state);
//			log("conducti  [%.3f][%.3f][%.3f][%.3f][%d]",_info->s_conductivity.ddl,_info->s_conductivity.tds,_info->s_conductivity.temp,_info->s_conductivity.yd,_info->s_conductivity.state);
//			log("flowmeter [%.3f][%.3f][%.3f][%.3f][%.3f][%.3f][%d]",_info->s_flowmeter.deep,_info->s_flowmeter.ll,_info->s_flowmeter.roll ,_info->s_flowmeter.speed ,_info->s_flowmeter.temp,_info->s_flowmeter.vertical,_info->s_flowmeter.state);
//			log("conducti  [%.3f][%.3f][%d]",_info->s_watergage.high ,_info->s_watergage.temp ,_info->s_watergage.state);		
//			
			

			
			//log("\r\n\r\n");
			
			return 1;
		}else{
			//log("空，没有数据可以读取");
		
			return 0;
		}

	  
}

