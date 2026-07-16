#include "dwin_app.h"
#include "dwin_drv.h"
#include "drv_dwin.h"

uint8_t buzzerbuff[]={0x5a,0xa5,0x05,0x82,0x00,0xa0,0x00,0x3E};	
void dwin_buzzer(void)
{
	
	(void)drv_dwin_write(buzzerbuff, 8U, 0xffU);
}







/*pwm addr 1116*/
/*
	25
	50 
	75
	100
*/
uint8_t dwin_set_pwm(uint8_t duty)
{
	uint8_t pagebuff[8]={0x5a,0xa5,0x05,0x82,0x11,0x16,0x00};

	if(duty>100)duty=100;
		

	pagebuff[7] = duty;
	(void)drv_dwin_write(pagebuff, 8U, 0xffU);

	
	return 1;
}





//F800 RED
//FFF0 YEW
//07E8 GRE

/*
	?: 0xB5B1

?: 0xC7B0

?: 0xB7E7

?: 0xC9C5

?: 0xD5BC

?: 0xBFD5

?: 0xB1C8

5: 0x35 (ASCII ??)

0: 0x30 (ASCII ??)

%: 0x25 (ASCII ??)
*/



//uint8_t pagebuff[10]={0x5a,0xa5,0x07,0x82,0x00,0x84,0x5a,0x01,0x00,0x00};
uint8_t dwin_set_page(uint8_t page)
{
	if(page>PAG_MAX)
		return 0;
	
	uint8_t temp[4]={0x5a,0x01,0x00,0x00};

	if(page==0)
	{
		temp[3]=0x00;
	}
	else if(page==1)
	{
		temp[3]=0x01;
	}
	
	if(dwin_write_block(0x0084,temp,4,1000))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


















































