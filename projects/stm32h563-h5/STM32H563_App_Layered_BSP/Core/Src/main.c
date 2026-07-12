/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "app_threadx.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp.h"
#include "bsp_clock.h"
#include "ldc_easy.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
//板子上有个显示屏，驱动芯片是ST7796U2  480*320RGB，引脚对应关系如下

//LCD信号		STM32引脚	说明
//VCC		VCC_3V3		3.3V供电
//GND		GND		地
//PWM		PB11		背光亮度控制
//LCD_RESET	PB4		LCD复位
//MOSI		PC1 	   	SPI数据输出
//SCK		PB10		SPI时钟
//RS (DC)		PD12		数据/命令选择
//CS		PD11 		SPI片选
//MISO		PC2		SPI数据输入

//触摸信号	STM32引脚	说明
//TP_RST	PB14	触摸芯片复位
//TP_INT	PB15	触摸中断
//TP_SDA	PB9	I2C数据
//TP_SCL	PB8	I2C时钟

//要求，按照刚才的BSP规范写出这个屏幕的驱动文件，开机默认点亮，D:\Embedded\cs\H5\STM32H563_App\user\ui这个路径下边有一些picture文件可以用


//bsp_xxx：规定“我要什么能力”
//mcu_xxx：负责“STM32H563 怎么实现”
//board_xxx：决定“这块板具体用哪个外设和引脚”

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
static void system_stop_on_error(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/** @brief Configure the status LED for startup and unrecoverable-error use. */
static void boot_led_hw_init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/** @brief Busy-wait without relying on interrupts or the HAL tick. */
static void system_stop_delay(void)
{
  volatile uint32_t cycles = SystemCoreClock / 8U;

  while(cycles > 0U)
  {
    cycles--;
    __NOP();
  }
}





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
//  boot_led_hw_init();

  /* USER CODE END Init */

  /* Configure the system clock */
  if(bsp_clock_configure_system() != BSP_STATUS_OK)
  {
    system_stop_on_error();
  }

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  /* USER CODE BEGIN 2 */
	if(bsp_init() != 0)
	{
	  system_stop_on_error();
	}


	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);
	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);

	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);
	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);


	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);
	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);

	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);
	bsp_dwt_delay_ms(200);
	bsp_ledn_toggle(0);
  /* USER CODE END 2 */

  app_threadx_init();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM17 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM17)
  {
    HAL_IncTick();
    ldc_easy_tick_all(1U);
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
static void system_stop_on_error(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  boot_led_hw_init();
  while (1)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_12);
    system_stop_delay();
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
