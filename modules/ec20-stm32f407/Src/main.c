/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rtc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "string.h"
#include "stdio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "log.h"
#include "bsp_usart.h"
#include "bsp_timer.h"
#include "debug_config.h"
#include "module_config.h"
#include "rs485_config.h"

#include "app_dht11.h"
#include "w25qxx.h"
#include "bsp_rtc.h"
#include "ec20_network.h"
#include "ec20_mqtt.h"
#include "app_pub_onenet.h"
#include "app_server.h"
#include "app_tlsf.h"
/*测试*/
#include "ec20_lib.h"
#include "app_gather.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void led_process(){
	
	HAL_Delay(1);
	static uint16_t cnt1=0;
		cnt1++;
		
		if(cnt1==500){
			cnt1=0;	
			HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_10);
		}
	
}

//#define MEM_POLL_SIZE	(1024*5)
//uint8_t g_malloc_mem_pool[MEM_POLL_SIZE];

#include "cJSON.h"
#include "tlsf.h"
/* USER CODE END 0 */



/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_SPI1_Init();
  MX_RTC_Init();
  MX_USART6_UART_Init();
  /* USER CODE BEGIN 2 */
	
	
	debug_serialport_init();
	module_serialport_init();
	rs485_serialport_init();
	bsp_usart_init();
	ec20_app_network_init();
	log("正在初始化端口...");
	app_tlsf_init();
	
	app_cjson_init();
	
//	uint8_t  buff[10]={0x1F, 0x05, 0x00, 0x00, 0x00, 0x00, 0xCE, 0x74 };
//	
//	rs485_write(buff, 8);
//	
	
//	modbus_send_05h(0x1F, 0x0000, 0x0000);
//	if(app_dht11_init()){
//	
//			log("dht11初始化成功");
//	}
//	app_upload_init();
//  app_gather_init();
	
//	W25QXX_Init();
	
// init_memory_pool(MEM_POLL_SIZE,g_malloc_mem_pool);
// 
// log("内存池初始化完毕");
// 
// log("内存池最大容量:%d",get_max_size(g_malloc_mem_pool));
// log("内存池可用容量:%d",get_used_size(g_malloc_mem_pool));
 
// for(int i=0;i<100;i++){
// 
//	 char * data = tlsf_malloc(256);
//	
//	 if(data){
//	 
//			sprintf(data,"hello yang le duo:%d",i);
//			log("data:[%s]",data);
//			tlsf_free(data);
//	 } 
// 
// }

//	cJSON_Hooks hooks;
//	hooks.free_fn=tlsf_free;
//	hooks.malloc_fn=tlsf_malloc;
//	
//	cJSON_InitHooks(&hooks);

//  

//	char * msg ="{\"leduo\":\"hahahaha\",\"lizi\":\"靓仔拉\",\"lizi_dick_length\":28}";//开始加key ,试试其他类型
//	
//	char *test ="{\"id\":\"1001\",\"name\":\"晓春\",\"sex\":\"男\",\"hobby\":{\"hobby1\":\"游泳\",\"hobby2\":\"打篮球\"}}";

//	cJSON * root =  cJSON_Parse(test);//这一步是序列化，把字符串，序列化成json结构，也就是生成一个json类，之后就是json类那样处理了，就不是字符串了。

//	if(root){ //先要保证 parse 成功
//	
//		//注意这个root，是最大的节点。
//		
//		//这个方法，用来获取key，需要填他的父节点，因为json能嵌套
//		cJSON * ld = cJSON_GetObjectItemCaseSensitive(root,"id");//获取 key = leduo 的value
//		cJSON * lz = cJSON_GetObjectItemCaseSensitive(root,"name");//获取 key = leduo 的value
//		cJSON * lzl = cJSON_GetObjectItemCaseSensitive(root,"sex");//获取 key = leduo 的value
//		//假设你已经知道他是字符串。
//		
//		//通过valuexxx 来获取他的值,注意先确认是否为null
//		
//		if(!cJSON_IsNull(ld) && cJSON_IsString(ld)){ //不为空，且是字符串
//		
//			log("id:%s",ld->valuestring);
//			
//		
//		}
//		
//		if(!cJSON_IsNull(lz) && cJSON_IsString(lz)){ //不为空，且是字符串
//		
//			log("name:%s",lz->valuestring);
//		
//		}		
//		
//		if(!cJSON_IsNull(lzl) && cJSON_IsString(lzl)){ //不为空，且是数字
//		
//			log("sex:%s",lzl->valuestring);//用的value int了哦。
//			
////			if(lzl->valueint==28){
////			
////					//modbus_device_jy_do(1,1);
////			}
//		
//		}
//		
//		
//		cJSON_Delete(root);//释放整个json结构。
//	
//	}else{
//	
//		log("parase error");
//	}

//	packet_data_test();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	
		debug_serialport_msg_process();
//		module_msg_process();
		rs485_serialport_process();
		
//		ec20_app_network_process();
//		ec20_app_mqtt_process();	
//		
//		app_dht11_process();	
		led_process();	
		
//		app_gather_process();
//		app_upload_process();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
