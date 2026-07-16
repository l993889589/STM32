#include "main.h"
#include "app_fan.h"
#include "debug_log.h"
#include "bsp_pwm.h"
#include "app_task.h"
#include "app_main.h"
#include "param.h"
fan_handle fan={0};

/*b
 * @brief 这里由adc采集到的数值，触发回调
 * 
 * @param level 
 */
void app_fan_set(uint8_t level)
{

//	if(fan.fan_state)
//	{
		switch(level)  
		{
			/*这里精度 要0-10000，占空比取反，所以想要实现10%在话，就写1000*/
//			case CLOSE:  		bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 0);	   debug_printf("触发回调,风扇占空比0%%,电压>0.9V\r\n");break;     //zheg
//			case LEVEL1:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 0);	 debug_printf("触发回调,风扇占空比0%%,电压>0.9V\r\n");break;
//			case LEVEL2:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 2500);	 debug_printf("触发回调,风扇占空比25%%,0.94V<电压<1.02V\r\n");break;
//			case LEVEL3:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 2500);	 debug_printf("触发回调,风扇占空比25%%,0.94V<电压<1.02V\r\n");break;
//			case LEVEL4:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 5000);	 debug_printf("触发回调,风扇占空比50%%,1.06V<电压<1.18V\r\n");break;
//			case LEVEL5:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 5000);	 debug_printf("触发回调,风扇占空比50%%,1.06V<电压<1.18V\r\n");break;
//			case LEVEL6:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 7500);	 debug_printf("触发回调,风扇占空比75%%,1.22V<电压<1.3V\r\n");break;
//			case LEVEL7:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 7500);	 debug_printf("触发回调,风扇占空比75%%,1.22V<电压<1.3V\r\n");break;
//			case LEVEL8:  	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,24500, 10000);	 debug_printf("触发回调,风扇占空比100%%,电压>1.33V\r\n");break;
//			case LEVEL9:  	bsp_SetTIMOutPWM(GPIOA, GPIO_Pin_8, TIM1, 1,24500, 7500);	 printf("触发回调,风扇占空比18%%,1.22V<电压<1.3V\r\n");break;
//			case LEVEL10:  	bsp_SetTIMOutPWM(GPIOA, GPIO_Pin_8, TIM1, 1,24500, 10000);	 printf("触发回调,风扇占空比100%%,电压=1.3V\r\n");break;
			//
			
			default:   break;
		}	
//	}

}


void app_fan_init(void)
{
	fan.state=0;
	fan.duty=param_fan_mode_get() ? param_pwm_manual_get() : param_pwm_auto_get();
	fan.fre=25000;
	bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,fan.fre, fan.duty);

	
}

void bsp_pwm_set(uint16_t duty)
{
		bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,25000, duty);
}

void app_fan_set_duty(uint16_t duty) { 


    // 限制占空比范围
    if (duty > 10000) {
        duty = 10000;
    } else if (duty < 4000) {
        duty = 4000;
    }

    if (param_fan_mode_get() != 1U)
        return;

    g_tVar.pwm_manual = duty;
	param_pwm_manual_set(duty);
	fan.duty = duty;
	bsp_pwm_set(duty);

}


	
void app_fan_set_duty_by_auto(uint16_t duty) { 

    // 限制占空比范围
    if (duty > 10000) {
        duty = 10000;
    } else if (duty < 4000) {
        duty = 4000;
    }
		
		
	if(param_fan_mode_get() != 0U)
		return;
	g_tVar.pwm_auto = duty;
	fan.duty = duty;
	bsp_pwm_set(duty);	

    
}


void app_fan_set_fre(uint32_t fre)
{
	fan.fre=fre;
	fan.state=1;
}


void app_fan_setall(uint32_t fre,uint16_t duty)
{
	if(duty >10000)
		duty=10000;
	fan.duty=duty;
	fan.fre=fre;
	fan.state=1;
}



void app_fan_process(void)
{
		if(fan.state)
		{	
			bsp_SetTIMOutPWM(GPIOA, GPIO_PIN_8, TIM1, 1,fan.fre, fan.duty);
			fan.state=0;		
		}
}

















