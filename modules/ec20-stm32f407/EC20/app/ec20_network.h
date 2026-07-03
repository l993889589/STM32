#ifndef _EC20_NETWORK_H_
#define _EC20_NETWORK_H_


#include "ec20_lib.h"


void 							ec20_app_network_tick(void);
void 							ec20_app_network_init(void);
void 							ec20_app_network_process(void);
uint8_t 					ec20_app_network_is_call(void);
device_imei_info  ec20_app_network_get_imei(void);
uint8_t 					ec20_app_network_get_imei_ex(device_imei_info	*imei);
device_iccid_info ec20_app_network_get_iccid(void);


#endif



