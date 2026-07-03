#ifndef  _DEBUG_CONFIG_H_
#define  _DEBUG_CONFIG_H_

#include "main.h"

void 	debug_serialport_init(void);
void 	debug_serialport_time_tick(void);
void 	debug_serialport_msg_process(void);
void 	wait_task(void);
int 	debug_block_read(uint8_t * buff,int buffsize,int timeout);
void 	debug_message_in(unsigned char *buf);


void 	debug_message_handler(unsigned char *message, int messagelen);
#endif

