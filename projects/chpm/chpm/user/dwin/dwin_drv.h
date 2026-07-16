#ifndef _DWIN_DRV_H_
#define _DWIN_DRV_H_

#include "stdint.h"


#define DATA_MAX 512
#define PAG_MAX  2
#define TIMEOUT		1000		/* 接收命令超时时间, 单位ms */
#define NUM			1			/* 循环发送次数 */






uint8_t dwin_write_block(uint16_t addr,uint8_t *data,uint8_t len,uint16_t timeout);


void dwin_buzzer(void);





void msg_analysis(unsigned char *message, int messagelen);

//uint8_t dwin_packet(uint16_t addr,uint8_t *data,uint8_t len,uint8_t *outdata,uint8_t actlen);
//uint8_t dwin_packet(uint16_t addr, uint8_t *data, uint8_t len, uint8_t *outdata, uint8_t actlen);
#endif

