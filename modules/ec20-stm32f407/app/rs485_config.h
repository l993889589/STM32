#ifndef _RS485_CFG_H_
#define _RS485_CFG_H_

#include "main.h"

void rs485_message_handler(unsigned char *message, int messagelen);

void rs485_serialport_init(void);
void rs485_serialport_timer_tick(void);
void rs485_serialport_process(void);
uint8_t rs485_block_read(uint8_t *buff,uint32_t size,uint32_t timeout);
void rs485_message_in(unsigned char *buf, unsigned int len);
void rs485_baud_set(uint32_t baud);



#endif





