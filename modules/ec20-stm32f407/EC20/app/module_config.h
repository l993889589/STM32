#ifndef  _MODULE_CONFIG_H_
#define  _MODULE_CONFIG_H_

#include "main.h"

void module_message_handler(unsigned char *message, int messagelen);

void module_serialport_init(void);
void module_serialport_time_tick(void);
void module_message_in(unsigned char *buf);
void module_msg_process(void);
void module_wait_task(void);
int  module_block_read(uint8_t * buff,int buffsize,int timeout);

#endif


