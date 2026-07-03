#ifndef __EC20_MQTT_H_
#define __EC20_MQTT_H_

#include "main.h"


void    ec20_app_mqtt_init(void);
void    ec20_app_mqtt_tick(void);
void    ec20_app_mqtt_process(void);
uint8_t ec20_app_mqtt_yeild(char *msg, int length);

uint8_t ec20_app_mqtt_pub(uint8_t channel,uint8_t * topic,uint8_t *msg,int length);
uint8_t ec20_app_mqtt_control(uint8_t state);

uint8_t ec20_app_mqtt_sub_onenet(void);


#endif




