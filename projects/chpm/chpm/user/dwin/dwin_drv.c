#include "dwin_drv.h"
#include "drv_dwin.h"
#include "string.h"
#include "debug_log.h"
#include "dwin_ldc_channel.h"
#include "tx_api.h"
#include "stdio.h"
#include <string.h>
#include <stdlib.h>




void float_to_big_endian(float value, uint8_t *output) {
    uint8_t *p = (uint8_t *)&value;
    output[0] = p[3];
    output[1] = p[2];
    output[2] = p[1];
    output[3] = p[0];
}


void uint16_to_big_endian(uint16_t value, uint8_t *output) {
    output[0] = (value >> 8) & 0xFF; // 高位字节
    output[1] = value & 0xFF;         // 低位字节
}

uint8_t CRC_NBUFF[]={0x5A,0xA5,0x03,0x82,0x4F,0x4B};
uint8_t CRC_BUFF[]={0x5A,0xA5,0x05,0x82,0x4F,0x4B,0xA5,0xEF};

static ULONG dwin_timeout_ticks(uint16_t timeout_ms)
{
	uint64_t ticks = ((uint64_t)timeout_ms * TX_TIMER_TICKS_PER_SECOND + 999ULL) /
	                 1000ULL;

	if(ticks == 0U)
		ticks = 1U;
	return ticks > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (ULONG)ticks;
}

uint8_t dwin_write_block(uint16_t addr,uint8_t *data,uint8_t len,uint16_t timeout)
{
	ULONG wait_ticks;
	if(len+2+1+3>=DATA_MAX)
		return 0;
		
	uint8_t  revbuff[20]={0};	
	uint8_t  sendbuff[DATA_MAX]={0x5a,0xa5};
	
	//5a a5 07 82 13 70 d2 ec b3 a3
	sendbuff[2]=len+1+2; //82=1  addr=2
	
	sendbuff[3]=0x82;
	
	sendbuff[4]=addr>>8;
	
	sendbuff[5]=addr&0xff;
	
	memcpy(sendbuff+6,data,len);
	
	wait_ticks = dwin_timeout_ticks(timeout);
	if(!dwin_ldc_channel_request_begin(wait_ticks))
		return 0;
	if(drv_dwin_write(sendbuff, (uint16_t)(len + 6), 0xffU) != BSP_STATUS_OK)
	{
		dwin_ldc_channel_request_end();
		return 0;
	}
	
	log_hex_msg("send to dwin:",sendbuff,len+6);

	if(dwin_ldc_channel_request_wait(revbuff, DATA_MAX, wait_ticks)==6)
	{
		//不带CRC  5A A5 03 824F 4B 带CRC 5A A5 05 82 4F 4B A5 EF
		if(memcmp(revbuff,CRC_NBUFF,6)==0)
		{
			dwin_ldc_channel_request_end();
			debug_printf("解析成功\r\n");
			return 1;
		}
			
		else
		{
			dwin_ldc_channel_request_end();
			return 0;
		}
		
	}
	else
	{
		dwin_ldc_channel_request_end();
		return 0;
	}
		
}



//uint8_t dwin_packet(uint16_t addr, uint8_t *data, uint8_t len, uint8_t *outdata, uint8_t actlen) {
//    if (len + 2 + 1 + 3 >= DATA_MAX) return 0;

//    uint8_t temp = 0;
//    outdata[temp++] = 0x5A;  // 包头
//    outdata[temp++] = 0xA5;
//    outdata[temp++] = len + 1 + 2;  // 数据长度 + 类型标志 + 地址
//    outdata[temp++] = 0x82;  // 类型标志
//    outdata[temp++] = addr >> 8;  // 地址高字节
//    outdata[temp++] = addr & 0xFF;  // 地址低字节
//    memcpy(outdata + temp, data, len);  // 数据部分
//    temp += len;

//    return temp;
//}


