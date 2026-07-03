#ifndef _BSP_USART_H__
#define _BSP_USART_H__

#include "main.h"

typedef struct
{
uint8_t log_flag;
//12312

}log_state;



int 	debug_printf(char *format, ...);
int 	rs485_printf(char *format, ...);
int 	bsp_ec20_printf(char *format, ...);
void 	debug_write(unsigned char *data, int length);
void 	bsp_ec20_write(unsigned char *data, int length);
void 	bsp_usart_init(void);
int 	rs485_printf(char *format, ...);
void 	rs485_write(unsigned char *data, int length);

uint8_t  rs485_set_baud(uint32_t BaudRate);

uint8_t set_usart_baud(UART_HandleTypeDef *huart, uint32_t BaudRate);

uint8_t app_log_control(uint8_t state);

void debug_log_init(void);

void rs485_tx_enable(uint8_t state);

int bsp_ec20_printf(char *format, ...);

#define XXX_SIZE		sizeof(log_state)

#endif



