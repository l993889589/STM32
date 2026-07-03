#include "app_at_frame.h"
#include "bsp_usart.h"


#include "string.h"



#define STRICT_VERIFICATION 0

int check_at_process(CMD_TASK g_atcmd_list[],int list_size,uint8_t *data, uint16_t data_size, DATA_IN_MODE at_in)
{
	//log("DATAIN:%d",at_in);

#if (STRICT_VERIFICATION)
	if (data_size < 2)
		return 0;

	if (data[data_size - 1] != '\n' || data[data_size - 2] != '\r')
	{
		log("error tail ,back");
		return 0;
	}
#endif


	for (int i = 0; i < list_size; i++)
	{
		if (g_atcmd_list[i].cmdhead == 0)
		{
			return 0;
		}

		if (g_atcmd_list[i].en_id & at_in)
		{

			if (strstr((char *)data, g_atcmd_list[i].cmdhead))
			{
				uint8_t headlen = strlen(g_atcmd_list[i].cmdhead);

				ATInfo info = {0};

				if (data[headlen] == '?')
				{
					info.mode = 1;
				}
				else if (data[headlen] == '=')
				{
					info.mode = 0;
				}

				//info.saveid=at_in;
				info.at_in = at_in;
				//log("info.at_in:[%d]",info.at_in);
				info.atcmd = (uint8_t *)g_atcmd_list[i].cmdhead;

				if (g_atcmd_list[i].overtask != 0)
				{
					if (g_atcmd_list[i].overtask(data, data_size, &info) == 1)
					{
						return 1;
					}
				}
			}
		}
	}
	return 0;
}
