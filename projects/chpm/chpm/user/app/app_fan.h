#ifndef _APP_FAN_H__
#define _APP_FAN_H__

#include "main.h"




typedef enum{
	
	CLOSE=0,
//	LEVEL1,   	//0.9   5%		0.90/3.3*4095=1117   // 5    
//	LEVEL2,   	//0.94	6%		0.94/3.3*4095=1166
//	LEVEL3,   	//0.98  7%		0.98/3.3*4095=1216
//	LEVEL4,   	//1.02  8%		1.02/3.3*4095=1266   //8
//	LEVEL5,   	//1.06  10%		1.06/3.3*4095=1315   //12
//	LEVEL6,   	//1.10	12%		1.10/3.3*4095=1365
//	LEVEL7,   	//1.14  14%		1.14/3.3*4095=1415
//	LEVEL8,   	//1.18  16%		1.18/3.3*4095=1464
//	LEVEL9,   	//1.22  18%		1.22/3.3*4095=1514
//	LEVEL10,    //1.3   20%  	1.30/3.3*4095=1613   //20
	
	
	LEVEL1,   		//0.9   0%		0.90/3.3*4095=1117   // 5    
	LEVEL2,   		//0.94	25%		0.94/3.3*4095=1166
	LEVEL3,   		//1.02  25%		0.98/3.3*4095=1216
	LEVEL4,   		//1.06  50%		1.06/3.3*4095=1315   //12
	LEVEL5,   		//1.18  50%		1.18/3.3*4095=1464
	LEVEL6,   		//1.22  75%		1.22/3.3*4095=1514
	LEVEL7,    		//1.3   75%  	1.30/3.3*4095=1613   //20
	LEVEL8,    		//1.33   100%  	1.30/3.3*4095=1613   //20	
	
}fan_level;

//#define LEVEL10_VALUE 1613  //设定的是1.3V关闭风扇，需要的时候可以改这里，值越大，阀值电压越高。
//#define LEVEL9_VALUE  1514
#define LEVEL8_VALUE  1650		//1.33V
#define LEVEL7_VALUE  1613		//1.3V
#define LEVEL6_VALUE  1514		//1.22V
#define LEVEL5_VALUE  1464		//1.18V
#define LEVEL4_VALUE  1315		//1.06V
#define LEVEL3_VALUE  1266		//1.02V
#define LEVEL2_VALUE  1166		//0.94V
#define LEVEL1_VALUE  1117		//0.9V



typedef struct {
	
	uint8_t  state;
	uint16_t duty;
	uint32_t fre;
	void(*app_fan_set)(uint8_t  level);
	
}fan_handle;


void app_fan_init(void);
void app_fan_set_duty(uint16_t duty);
void app_fan_set_fre(uint32_t fre);
void app_fan_setall(uint32_t fre,uint16_t duty);
void app_fan_process(void);

void app_fan_set_duty_by_auto(uint16_t duty) ;







#endif
