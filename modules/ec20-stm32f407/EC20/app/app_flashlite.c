#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include "app_flashlite.h"
#include "app_flashid.h"

#include "main.h"

#define USER_SPACE_START_ADD    0


typedef struct
{

    uint16_t header_info;

    uint16_t header_info_;

} FLASH_HEADER;


void drv_flashlite_read(uint32_t addr,uint8_t  *buff, uint16_t  size)
{
	W25QXX_Read(buff,addr,size);
}


void drv_flashlite_write(uint32_t addr,uint8_t  *buff, uint16_t  size)
{
	W25QXX_Write(buff,addr,size);
}


unsigned int gsdk_get_usr_space_start_addr(){

    return USER_SPACE_START_ADD;

}

int app_flash_read(int id, uint8_t *readbuff, uint16_t maxsize)
{
    uint16_t listsize = get_flash_list_size();

    unsigned int add = gsdk_get_usr_space_start_addr();

    FLASH_HEADER header_info = {0};

    for (int i = 0; i < listsize; i++)
    {
        if (get_flash_list_handle()[i].id == id)
        {
            drv_flashlite_read(add, (uint8_t *)&header_info, sizeof(FLASH_HEADER));

            if (header_info.header_info == (uint16_t)(~header_info.header_info_))
            {
                if (header_info.header_info <= get_flash_list_handle()[i].reservedspace)
                {

                    if (maxsize <= header_info.header_info)
                    {
                        memset(readbuff, 0, maxsize);
                        drv_flashlite_read(add + sizeof(FLASH_HEADER), readbuff, maxsize);
                    }
                    else
                    {
                        memset(readbuff, 0, maxsize);
                        drv_flashlite_read(add + sizeof(FLASH_HEADER), readbuff, header_info.header_info);
                    }

                    return header_info.header_info;
                }
            }
            else
            {
                return 0;
            }
        }
        else
        {
            add += get_flash_list_handle()[i].reservedspace + sizeof(FLASH_HEADER);
        }
    }

    return 0;
}

int app_flash_write(int id, uint8_t *writebuff, uint16_t size)
{
    uint16_t listsize = get_flash_list_size();

    uint8_t write_buff[size + sizeof(FLASHINFO)] ;
		memset(write_buff,0,size + sizeof(FLASHINFO));

    unsigned int add = gsdk_get_usr_space_start_addr();

    FLASH_HEADER header_info = {0};

    for (int i = 0; i < listsize; i++)
    {
        if (get_flash_list_handle()[i].id == id)
        {
            if (size)
            {
                if (size > get_flash_list_handle()[i].reservedspace)
                    size = get_flash_list_handle()[i].reservedspace;

                header_info.header_info = size;
                header_info.header_info_ = ~size;

                memcpy(write_buff, (uint8_t *)&header_info, sizeof(FLASH_HEADER));
                memcpy(write_buff + sizeof(FLASH_HEADER), writebuff, size);
                drv_flashlite_write(add , write_buff, size + sizeof(FLASH_HEADER));
            }
            else
            {
               //表示擦除
                header_info.header_info = 0;
                header_info.header_info_ = 0;

                drv_flashlite_write(add, (uint8_t *)&header_info, sizeof(FLASH_HEADER));
            }

            return size;
        }
        else
        {
            add += get_flash_list_handle()[i].reservedspace + sizeof(FLASH_HEADER);
        }
    }

    return 0;
}

int app_flash_clear()
{
    uint16_t listsize = get_flash_list_size();

    for (int i = 0; i < listsize; i++)
    {
        app_flash_write(get_flash_list_handle()[i].id, 0, 0);
    }
    return 1;
}

