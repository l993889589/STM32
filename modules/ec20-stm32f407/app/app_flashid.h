#ifndef _APP_FLASHLID_H_
#define _APP_FLASHLID_H_

#include "app_flashlite.h"

typedef enum
{
    RS485_BUAD,//  波特率
		ONENET_ADDRESS,//onenet 服务器地址
		ONENET_PORT,//onenet 端口号
		ONENET_CLIENTID,//设备mqtt链接参数
		ONENET_USERNAME,//设备mqtt链接参数
		ONENET_PASSWORD,//设备mqtt链接参数
		
		ALIAUTH_ADDRESS,//onenet 服务器地址
		ALIAUTH_PORT,//onenet 端口号
		ALIAUTH_CLIENTID,//设备mqtt链接参数
		ALIAUTH_USERNAME,//设备mqtt链接参数
		ALIAUTH_PASSWORD,//设备mqtt链接参数
		
		OPERATION_MODE,//设备工作模式，自动 0 ，手动 1
		MANUAL_MODE_ACQ_FRE,//手动模式的采集频率
	  MANUAL_MODE_TRANS_FRE,//手动模式的传输频率
	  AUTO_MODE_ACQ_FRE,//自动模式的默认采集频率
		AUTO_MODE_TRANS_FRE,//自动默认传输频率
		THRESHOLD_LOW,//阈值小值
		THRESHOLD_HIGH,//阈值大值
		RADAR_BLIND,//雷达盲区数据
		PACKET_INFO,
		LOG_EN,
	
}FLASHID;

#define ONENET_ADDRESS_LEN	50
#define	ONENET_CLIENTID_LEN 50
#define	ONENET_USERNAME_LEN 50
#define	ONENET_PASSWORD_LEN 50


#define ALIAUTH_ADDRESS_LEN	 50
#define	ALIAUTH_CLIENTID_LEN 50
#define	ALIAUTH_USERNAME_LEN 50
#define	ALIAUTH_PASSWORD_LEN 50



#define TO_STRING(x) #x




int get_flashid_size(int id);
int get_flash_list_size(void);
FLASHINFO * get_flash_list_handle(void);

#endif




