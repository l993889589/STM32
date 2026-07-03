#ifndef _APP_GATHER_H_
#define _APP_GATHER_H_

#include "main.h"

#include "app_info.h"

uint8_t app_gather_init(void);
void 		app_gather_tick(void);
void 		app_gather_process(void);


uint8_t control_device_jy_dam_relay(uint8_t addr,uint8_t channel,uint8_t state);


uint8_t querry_sht30_device(sensor_sht30 *s_sht30 );

uint8_t querry_sht30_device_JY_WS1(sensor_sht30 *s_sht30 );

uint8_t querry_device_JY_DAM(sensor_jy_dam *s_dam0404d );


void app_querry_sht30(void);
uint8_t app_querry_device_JY_DAM(void);









#endif


