#include "ec20_pub.h"
#include "log.h"
#include "ec20_network.h"
#include "ec20_mqtt.h"
#include "ec20_lib.h"

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

/*onenet $dp*/

uint8_t ec20_pub_device_info(){

		uint8_t buff[1024]={0};
	
		device_imei_info imei={0};
	
		imei= ec20_app_network_get_imei();
		
		int ret =sprintf((char*)buff,"{\"id\": \"123\",\"version\": \"1.0\",\"params\": {\"relay1\": {\"value\": true},\"relay2\": {\"value\": true},\"relay3\": {\"value\": true},\"relay4\": {\"value\": true},\"imei\": {\"value\": \"%s\"}}}",imei.imei);
	
}