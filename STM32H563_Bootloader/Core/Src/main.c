/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : OTA bootloader entry
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "dcache.h"
#include "icache.h"
#include "memorymap.h"
#include "spi.h"
#include "gpio.h"
#include "gd25lq128.h"
#include "ota_boot.h"
#include "ota_layout.h"
#include "app_threadx.h"
#include "boot_shell.h"

void SystemClock_Config(void);

static void Boot_LedInit(void);
static void Boot_LedBlink(uint32_t on_ms, uint32_t off_ms);
static void Boot_DelayMs(uint32_t ms);
static uint8_t Boot_AppIsValid(void);
static void Boot_JumpToApp(void);

static void Boot_LedInit(void)
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

static void Boot_LedBlink(uint32_t on_ms, uint32_t off_ms)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);
  Boot_DelayMs(on_ms);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);
  Boot_DelayMs(off_ms);
}

static void Boot_DelayMs(uint32_t ms)
{
  while(ms-- != 0U)
  {
    for(volatile uint32_t i = 0U; i < 64000U; i++)
    {
      __NOP();
    }
  }
}

static uint8_t Boot_AppIsValid(void)
{
  const uint32_t app_sp = *(volatile uint32_t *)OTA_APP_BASE;
  const uint32_t app_reset = *(volatile uint32_t *)(OTA_APP_BASE + 4U);
  const uint32_t sram_end = OTA_SRAM_BASE + OTA_SRAM_SIZE;
  const uint32_t flash_end = OTA_INTERNAL_FLASH_BASE + OTA_INTERNAL_FLASH_SIZE;

  if((app_sp < OTA_SRAM_BASE) || (app_sp >= sram_end))
  {
    return 0U;
  }

  if((app_reset < OTA_APP_BASE) || (app_reset >= flash_end))
  {
    return 0U;
  }

  if((app_reset & 1U) == 0U)
  {
    return 0U;
  }

  return 1U;
}

static void Boot_JumpToApp(void)
{
  const uint32_t app_sp = *(volatile uint32_t *)OTA_APP_BASE;
  const uint32_t app_reset = *(volatile uint32_t *)(OTA_APP_BASE + 4U);
  void (*app_entry)(void) = (void (*)(void))app_reset;

  __disable_irq();

  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;

  for(uint32_t i = 0U; i < 8U; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }

  SCB->VTOR = OTA_APP_BASE;
  __DSB();
  __ISB();

  __set_CONTROL(0U);
  __set_MSP(app_sp);
  __DSB();
  __ISB();
  __enable_irq();
  app_entry();
}

int main(void)
{
  gd25lq128_id_t id;
  ota_boot_result_t boot_result = OTA_BOOT_RESULT_NO_UPDATE;

  HAL_Init();
  Boot_LedInit();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_ICACHE_Init();
  MX_DCACHE1_Init();
  MX_SPI1_Init();

  if(gd25lq128_read_id(&id))
  {
    Boot_LedBlink(80U, 80U);
    Boot_LedBlink(80U, 200U);
    boot_result = ota_boot_process_update();
    if(boot_result == OTA_BOOT_RESULT_INSTALLED)
    {
      Boot_LedBlink(60U, 60U);
      Boot_LedBlink(60U, 60U);
      Boot_LedBlink(60U, 200U);
    }
  }
  else
  {
    Boot_LedBlink(400U, 400U);
  }

  if(boot_result != OTA_BOOT_RESULT_RECOVERY_REQUIRED && Boot_AppIsValid())
  {
    Boot_JumpToApp();
  }

  boot_shell_set_boot_result(boot_result);
  MX_ThreadX_Init();

  while(1)
  {
    Boot_LedBlink(100U, 900U);
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_CRS_CLK_ENABLE();

  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000,1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;

  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);

  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if(htim->Instance == TIM17)
  {
    HAL_IncTick();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while(1)
  {
    Boot_LedBlink(50U, 50U);
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif
