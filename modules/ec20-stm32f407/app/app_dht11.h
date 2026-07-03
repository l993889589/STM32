#ifndef __APP_DHT11_H_
#define __APP_DHT11_H_

#include "main.h"

#define DHT11_OK 0
#define DHT11_ERROR 1
#define DHT11_TIMEOUT 2

// Define the time constants for reading bits
#define DHT11_START_TIME 18 // ms
#define DHT11_RESPONSE_TIME 40 // us
#define DHT11_BIT_TIME 26 // us


typedef  struct
{
	uint8_t temperature;
	uint8_t humidity;
}sensor_dht11;


uint8_t dht11_read_bit(void);           //读取一个比特
uint8_t dht11_read_byte(void);					//读取一个字节


uint8_t 			dht11_check(void);
uint8_t 			dht11_read_data(sensor_dht11 *dht11);
uint8_t 			dht11_read_data_test(uint8_t *h);


uint8_t 			app_dht11_init(void);			//初始化函数，启动了一个6s的定时任务，并返回check结果
void 					app_dht11_tick(void);			//定时任务的tick
void 					app_dht11_process(void);	//定时任务的线程
sensor_dht11 	dht11_data_access(void);  //读取dht11的数据

#endif
