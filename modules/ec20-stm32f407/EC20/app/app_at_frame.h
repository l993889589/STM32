
#ifndef APP_AT_FRAME_H
#define APP_AT_FRAME_H

#include "main.h"

typedef enum
{

	USART_AT_IN = 1,

	TCP_AT_IN = 0x1 << 1,

} DATA_IN_MODE;

typedef struct _ATInfo
{

	uint8_t mode;

	uint8_t saveid;

	uint8_t *atcmd;

	DATA_IN_MODE at_in;

} ATInfo;

typedef struct
{

	char *cmdhead;

	int (*overtask)(uint8_t *message,uint16_t size, ATInfo *info);

	uint8_t en_id;

} CMD_TASK;




int check_at_process(CMD_TASK g_atcmd_list[], int list_size, uint8_t *data, uint16_t data_size, DATA_IN_MODE at_in);



#endif
