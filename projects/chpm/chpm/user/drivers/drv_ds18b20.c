#include "drv_ds18b20.h"
#include "main.h"
#include "tim.h"
#include "debug_log.h"






#define TIM_HANDLE 	&htim4


#define DS18B20_Pin  GPIO_PIN_0
#define DS18B20_GPIO_Port GPIOB



void DS18B20_DQ_OUT(uint8_t n){

	

			if(n){
			
			HAL_GPIO_WritePin(DS18B20_GPIO_Port,DS18B20_Pin,GPIO_PIN_SET);
			
			}else{
				
				HAL_GPIO_WritePin(DS18B20_GPIO_Port,DS18B20_Pin,GPIO_PIN_RESET);
			}
	
	

}	

uint8_t DS18B20_DQ_IN()
{


		
		return	HAL_GPIO_ReadPin(DS18B20_GPIO_Port,DS18B20_Pin);
	

	
}	


static void DS18B20_IO_IN(void)
{

			
			GPIO_InitTypeDef GPIO_InitStruct = {0};
			GPIO_InitStruct.Pin = DS18B20_Pin;
			GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
			GPIO_InitStruct.Pull = GPIO_PULLUP;
			GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH ;
			HAL_GPIO_Init(DS18B20_GPIO_Port, &GPIO_InitStruct);		
		
		


}


static void DS18B20_IO_OUT(void)
{

		
		GPIO_InitTypeDef GPIO_InitStruct = {0};
		GPIO_InitStruct.Pin = DS18B20_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH ;
    HAL_GPIO_Init(DS18B20_GPIO_Port, &GPIO_InitStruct);
	
	
			
}



void delay_us(int us){
	
	//HAL_TIM_Base_Start(TIM_HANDLE);
	
	__HAL_TIM_ENABLE(TIM_HANDLE);
  __HAL_TIM_SET_COUNTER(TIM_HANDLE, 0); //把计数器的值设置为0
	//__HAL_TIM_ENABLE(TIM_HANDLE); //开启计数
	while (__HAL_TIM_GET_COUNTER(TIM_HANDLE) < us); //每计数一次，就是1us，直到计数器值等于我们需要的时间
	__HAL_TIM_DISABLE(TIM_HANDLE); //关闭计数
	//HAL_TIM_Base_Stop(TIM_HANDLE);	
	
}

//复位DS18B20
void DS18B20_Rst(void)	   
{                 
	  DS18B20_IO_OUT();   //设置为输出
    DS18B20_DQ_OUT(0);  //拉低DQ
    delay_us(750);      //拉低750us
    DS18B20_DQ_OUT(1);  //DQ=1 
	  delay_us(15);       //15US
}

//等待DS18B20的回应
//返回1:未检测到DS18B20的存在
//返回0:存在
uint8_t DS18B20_Check(void) 	   
{   
	uint8_t retry=0;
	DS18B20_IO_IN();    //设置为输入
    while (DS18B20_DQ_IN()&&retry<200)
	{
		retry++;
		delay_us(1);
	};	 
	if(retry>=200)return 1;
	else retry=0;
    while (!DS18B20_DQ_IN()&&retry<240)
	{
		retry++;
		delay_us(1);
	};
	if(retry>=240)return 1;	    
	return 0;
}

//从DS18B20读取一个位
//返回值：1/0
uint8_t DS18B20_Read_Bit(void) 
{
    uint8_t data;
	DS18B20_IO_OUT();   //设置为输出
    DS18B20_DQ_OUT(0); 
	delay_us(2);
    DS18B20_DQ_OUT(1); 
	DS18B20_IO_IN();    //设置为输入
	delay_us(12);
	if(DS18B20_DQ_IN())data=1;
    else data=0;	 
    delay_us(50);           
    return data;
}

//从DS18B20读取一个字节
//返回值：读到的数据
uint8_t DS18B20_Read_Byte(void)   
{        
    uint8_t i,j,dat;
    dat=0;
	for (i=1;i<=8;i++) 
	{
        j=DS18B20_Read_Bit();
        dat=(j<<7)|(dat>>1);
    }						    
    return dat;
}



//写一个字节到DS18B20
//dat：要写入的字节
void DS18B20_Write_Byte(uint8_t dat)     
 {             
    uint8_t j;
    uint8_t testb;
    DS18B20_IO_OUT();     //设置为输出
    for (j=1;j<=8;j++) 
	{
        testb=dat&0x01;
        dat=dat>>1;
        if(testb)       // 写1
        {
            DS18B20_DQ_OUT(0);
            delay_us(2);                            
            DS18B20_DQ_OUT(1);
            delay_us(60);             
        }
        else            //写0
        {
            DS18B20_DQ_OUT(0);
            delay_us(60);             
            DS18B20_DQ_OUT(1);
            delay_us(2);                          
        }
    }
}
 
 
//开始温度转换
bool DS18B20_StartConversion(void)
{   						               
    DS18B20_Rst();	   
    if(DS18B20_Check() != 0U)
        return false;
    DS18B20_Write_Byte(0xcc);// skip rom
    DS18B20_Write_Byte(0x44);// convert
    return true;
}

//初始化DS18B20的IO口 DQ 同时检测DS的存在
//返回1:不存在
//返回0:存在    	 
bool DS18B20_Init(void)
{
		__HAL_RCC_GPIOB_CLK_ENABLE();	
	
		GPIO_InitTypeDef GPIO_InitStruct = {0};
		GPIO_InitStruct.Pin = DS18B20_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
		GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH ;
    HAL_GPIO_Init(DS18B20_GPIO_Port, &GPIO_InitStruct);
	
	
 	DS18B20_Rst();
	return DS18B20_Check() == 0U;
}

//从ds18b20得到温度值
//精度：0.1C
//返回值：温度值 （-550~1250） 
static uint8_t ds18b20_crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0U;

    while(length--)
    {
        uint8_t value = *data++;
        for(uint8_t bit = 0U; bit < 8U; bit++)
        {
            uint8_t mix = (crc ^ value) & 0x01U;
            crc >>= 1;
            if(mix)
                crc ^= 0x8CU;
            value >>= 1;
        }
    }
    return crc;
}

bool DS18B20_ReadTemperature(float *temperature)
{
    uint8_t scratchpad[9];
    int16_t raw;
    float measured;

    if(!temperature)
        return false;
    DS18B20_Rst();
    if(DS18B20_Check() != 0U)
        return false;
    DS18B20_Write_Byte(0xCCU);
    DS18B20_Write_Byte(0xBEU);
    for(uint8_t i = 0U; i < sizeof(scratchpad); i++)
        scratchpad[i] = DS18B20_Read_Byte();

    if(ds18b20_crc8(scratchpad, 8U) != scratchpad[8])
        return false;
    raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    measured = (float)raw * 0.0625f;
    if(measured < -55.0f || measured > 125.0f || measured == 85.0f)
        return false;
    *temperature = measured;
    return true;
}



