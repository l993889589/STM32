#include "app_dht11.h"

#include "main.h"

#include "bsp_usart.h"

#include "bsp_timer.h"

#include "log.h"

#include "timer_task.h"

/*
*		注意引脚替换的时候要把out in函数中的引脚换掉dht11_data_out(void)/dht11_data_in(void)
*/

#define DHT11_READ_PIN  	HAL_GPIO_ReadPin(GPIOD,GPIO_PIN_13)
#define DHT11_PIN_SET   	HAL_GPIO_WritePin(GPIOD,GPIO_PIN_13,GPIO_PIN_SET)
#define DHT11_PIN_RESET 	HAL_GPIO_WritePin(GPIOD,GPIO_PIN_13,GPIO_PIN_RESET)

/*全局变量初始化*/
sensor_dht11 dht11={0};
timer_handle dht11_call={0};

sensor_dht11 dht11_data_access(void)
{
	return  dht11;
}


uint8_t dht11_read_bit(void)
{
	uint8_t retry=0;
	/*先等待变低最多80us*/
	while(DHT11_READ_PIN && retry<100)
	{
		retry++;
		bsp_delay_us(1);
	}
	
	retry=0;
	/*等待50us变高*/
	while(!DHT11_READ_PIN && retry<100)
	{
		retry++;
		bsp_delay_us(1);
	}
	
	bsp_delay_us(40);
	
	if(DHT11_READ_PIN)
	{
		return 1;
	}
	else
	{
		return 0;	
	}

}


uint8_t dht11_read_byte(void)
{
	uint8_t i,dat;
	
	dat=0;

	for(i=0;i<8;i++)
	{
//		dat<<=1;
//		dat|=dht11_read_bit();
	dat |=dht11_read_bit()<<(7-i);
	}
	
	return dat;
}


/*
 * 更改DHT11引脚方向
 */
// 输出output
void dht11_data_out(void)
{
	  GPIO_InitTypeDef GPIO_InitStruct = {0};

	  /*Configure GPIO pin : PB12 */
	  GPIO_InitStruct.Pin = GPIO_PIN_13;
	  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		GPIO_InitStruct.Pull=GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

// 输入input
void dht11_data_in(void)
{
	  GPIO_InitTypeDef GPIO_InitStruct = {0};

	  /*Configure GPIO pin : PB12 */
	  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	  GPIO_InitStruct.Pin = GPIO_PIN_13;
	  GPIO_InitStruct.Pull = GPIO_PULLUP;
	  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void dht11_rst(void)
{
	dht11_data_out();
	DHT11_PIN_RESET;
	HAL_Delay(20);
	DHT11_PIN_SET;
	bsp_delay_us(30);
}


/*返回1，可以读数据了*/
uint8_t dht11_check(void)
{
	uint8_t  retry = 0;
	
	/*发送起始信号，拉低最少18ms*/
	dht11_data_out();
	DHT11_PIN_RESET;
	HAL_Delay(20);
	/*等待20~40us*/
	DHT11_PIN_SET;
	bsp_delay_us(30);
	
	/*切换到输入模式*/
	dht11_data_in();
	/*读取DHT11响应*/
	while(DHT11_READ_PIN && retry<100)
	{
		retry++;
		bsp_delay_us(1);
	}
	
	if(retry>100)
	{
		return 0;
	}
	else
	{
		retry=0;		
	}
	/*等待拉高*/
	while(!DHT11_READ_PIN && retry<100)
	{
		retry++;
		bsp_delay_us(1);
	}
	/*没有读到高电平*/
	if(retry>100)
	{
		return 0;
	}

	return 1;
}








/*初始化，10s读取一次数据,suspend任务，返回check结果*/
uint8_t app_dht11_init(void)
{
	timer_task_reg(& dht11_call,5000);
	timer_task_suspend(& dht11_call,0);
	return dht11_check();
}


void app_dht11_tick(void)
{

	timer_task_tick(&dht11_call);
}


void app_dht11_process(void)
{
	
	if( timer_task_iscall(&dht11_call))
	{
		dht11_read_data(&dht11);
	}
}


uint8_t dht11_read_data(sensor_dht11 *dht11)
{
			
	uint8_t buff[5]={0};
	if(dht11_check())
	{
	
		for(int i=0;i<5;i++)
		{

			 buff[i]=dht11_read_byte();
		}
		

		if(((buff[0]+buff[1]+buff[2]+buff[3])&0xff)==buff[4])
		{	
			dht11->humidity=buff[0];
		  dht11->temperature=buff[2];
			log("dht11->humidity=%d,dht11->temperature=%d\r\n",dht11->humidity,dht11->temperature);
			return DHT11_OK;
			
			
		}else{
			
			return DHT11_ERROR;
			
		}
	}
	else
	{
		log("dht11数据读取失败,请检查模块是否正常\r\n");
	}
}




uint8_t dht11_read_data_test(uint8_t *h)
{
	
	uint8_t buff[5]={0};
	
//	dht11_rst();
	
	
	if(dht11_check())
	{
		
		for(int i=0;i<5;i++)
		{
			
			 buff[i]=dht11_read_byte();
			
		}
		
		debug_printf("2-》》》》buff[0]=%d,buff[1]=%d,buff[2]=%d,buff[3]=%d\r\n",buff[0],buff[1],buff[2],buff[3]);
		if(((buff[0]+buff[1]+buff[2]+buff[3])&0xff)==buff[4])
		{
			
			log("dht11_data check in");
            *h=buff[0]; //将湿度值放入指针1
							h++;
            *h=buff[2]; //将温度值放入指针2
			debug_printf("dht11->humidity=%d,dht11->temperature=%d\r\n",buff[0],buff[2]);
			
		}
	}
}






