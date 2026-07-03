#ifndef _APP_INFO_
#define _APP_INFO_

#include "main.h"


typedef struct{
	
	float temp;
	float humi;

}sensor_sht30;


typedef struct{
	
	uint8_t state1;
	uint8_t state2;
	uint8_t state3;
	uint8_t state4;
	
	

}sensor_jy_dam;


typedef struct{
	
	float vbat;//电压
	uint8_t csq;//信号强度
	
}device_info;

typedef struct{
	
	float ddl;//电导率
	float tds;//tds
	float yd;//盐度
	float temp;//温度
	uint8_t state;

}sensor_conductivity;

typedef struct{
	
	float ll;//流量
	float speed;//流速
	float deep;//水深
	float roll;//横滚角
	float vertical;//垂直角
	float temp;//温度	
	uint8_t state;
	
}sensor_flowmeter;

typedef struct{
	
	float high;//水深
	float temp;//温度
	uint8_t state;
	
}sensor_watergage;

typedef struct{
	
	uint16_t head;//指向头位置，
	uint16_t tail;//指向尾位置
	uint16_t max_set;
	
}packet_info_flash_handle;

typedef struct{
	
	float high;
	float roll;//横滚角
	float vertical;//垂直角
	float temp;
	uint8_t state;
	
}sensor_airheight;



typedef struct{
	
	sensor_airheight s_airheight;
	sensor_watergage s_watergage;
	sensor_flowmeter s_flowmeter;
	sensor_conductivity s_conductivity;
	device_info d_info;
	
	uint32_t time_samp;
	uint8_t state;//按位存储每个结构体的数据是否准备好
	
}packet_info;//推送的时候再额外带个iccid和imei



void print_packet_size(void);


#define PACKET_INFO_SIZE		sizeof(packet_info)

void packet_info_add(packet_info *_info);
uint8_t packet_info_read_old(packet_info *_info);
void packet_info_next(void);
void packet_info_init(void);
void put_packet_info(void);
void up_config_packet_gen(uint8_t *buff);
uint8_t put_packet_ishave(void);
//推送数据到数据点
uint8_t put_flash_config_info(void);
uint8_t write_packet_info(packet_info *info);
void packet_init(void);

void packet_info_clear(void);
void packet_data_test(void);


#endif












