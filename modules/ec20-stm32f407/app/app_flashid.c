#include "app_flashid.h"
#include "app_flashlite.h"


//		ONENET_ADDRESS,//onenet 服务器地址
//		ONENET_PORT,//onenet 端口号
//		ONENET_CLIENTID,//设备mqtt链接参数
//		ONENET_USERNAME,//设备mqtt链接参数
//		ONENET_PASSWORD,//设备mqtt链接参数
//		OPERATION_MODE,//设备工作模式，自动 0 ，手动 1
//		MANUAL_MODE_ACQ_FRE,//手动模式的采集频率
//	  MANUAL_MODE_TRANS_FRE,//手动模式的传输频率
//	  AUTO_MODE_ACQ_FRE,//自动模式的默认采集频率
//		AUTO_MODE_TRANS_FRE,//自动默认传输频率
//		THRESHOLD_LOW,//阈值小值
//		THRESHOLD_HIGH,//阈值大值
//		RADAR_BLIND,//雷达盲区数据


const FLASHINFO g_flashlist[] = {

		{4			 						,   RS485_BUAD		  },
		{ONENET_ADDRESS_LEN	,   ONENET_ADDRESS  },
		{4 		   						,   ONENET_PORT 		},		
		{ONENET_CLIENTID_LEN,   ONENET_CLIENTID },
		{ONENET_USERNAME_LEN,   ONENET_USERNAME },
		{ONENET_PASSWORD_LEN,   ONENET_PASSWORD },
		{4									,   OPERATION_MODE},		
		{4 									,   MANUAL_MODE_ACQ_FRE},		
		{4 									,   MANUAL_MODE_TRANS_FRE},
	
		{4 									,   AUTO_MODE_ACQ_FRE},		
		{4 									,   AUTO_MODE_TRANS_FRE},		
		
		{4 									,   THRESHOLD_LOW},		
		{4 									,   THRESHOLD_HIGH},		
		{4 									,   RADAR_BLIND},		
		
		{4 									,   LOG_EN},		
		{ALIAUTH_ADDRESS_LEN	, ALIAUTH_ADDRESS  },
		{4 		   						,   ALIAUTH_PORT 		 },		
		{ALIAUTH_CLIENTID_LEN,  ALIAUTH_CLIENTID },
		{ALIAUTH_USERNAME_LEN,  ALIAUTH_USERNAME },
		{ALIAUTH_PASSWORD_LEN,  ALIAUTH_PASSWORD },		
		
//		{4 									,   RELAY_1_TIME_EN},		
//		{4 									,   RELAY1_FRE},	
		

		
	//	{PACKET_INFO_SIZE ,   PACKET_INFO},			
		
		
};


FLASHINFO * get_flash_list_handle(){
	
		return (FLASHINFO *)g_flashlist;
}


int get_flash_list_size(){
	
		return sizeof(g_flashlist)/sizeof(FLASHINFO);
}



int get_flashid_size(int id)
{
	int listsize = sizeof(g_flashlist) / sizeof(FLASHINFO);

	for (int i = 0; i < listsize; i++)
	{
		if (g_flashlist[i].id == id)
		{
			return g_flashlist[i].reservedspace;
		}
	}

	return 0;
}





