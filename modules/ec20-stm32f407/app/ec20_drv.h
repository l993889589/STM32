#ifndef __EC20_DRV_H_
#define __EC20_DRV_H_
#include "main.h"




int 			ec20_drv_write(uint8_t *buff,int len);
uint32_t 	ec20_drv_printf(char *format, ...);
void 			ec20_drv_writestr(char *data);
int 			ec20_drv_blockread(uint8_t *buff,uint32_t size,uint32_t timeout);


uint32_t drv_power_on(void);
uint32_t drv_power_off(void);

#endif




