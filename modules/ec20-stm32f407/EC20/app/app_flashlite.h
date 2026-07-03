#ifndef  _APP_FLASHLITE_H_
#define  _APP_FLASHLITE_H_

#include "w25qxx.h"


//#define  _flashlite_write(add,buff,size)  bsp_flash_write(add,buff,size)
//#define  _flashlite_read(add,buff,size) 	bsp_flash_read(add,buff,size)

//#define _flashlite_write(buff,add,size)  W25QXX_Write(buff,add,size)
//#define _flashlite_read(buff,add,size)   W25QXX_Read(buff,add,size)






typedef struct{
	
	unsigned short reservedspace;
	unsigned short id;
	
}FLASHINFO;

int app_flash_read(int id,unsigned char *readbuff,unsigned short maxsize);
int app_flash_write(int id,unsigned char *writebuff,unsigned short size);
int app_flash_clear(void);


#endif




