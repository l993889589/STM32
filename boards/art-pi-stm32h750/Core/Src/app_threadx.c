/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2020-2021 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_ap6212_bridge.h"
#include "app_ap6212_sdio_probe.h"
#include "app_config.h"
#include "app_netxduo.h"
#include "app_uart4_console.h"
#include "app_uart4_echo.h"
#include "bsp.h"
#include "main.h"
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
static TX_THREAD app_led_thread;
static UCHAR app_led_thread_stack[512];
static volatile UINT app_service_init_status = 0xFFFFFFFFU;
void app_led_task_entry(ULONG thread_input);



/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  /* USER CODE BEGIN App_ThreadX_MEM_POOL */
	TX_PARAMETER_NOT_USED(memory_ptr);
  /* USER CODE END App_ThreadX_MEM_POOL */

  /* USER CODE BEGIN App_ThreadX_Init */
  bsp_init();

  if(tx_thread_create(&app_led_thread, "LED Blink",
                      app_led_task_entry, 0U,
                      app_led_thread_stack, sizeof(app_led_thread_stack),
                      20U, 20U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

#if APP_ENABLE_UART4_LDC_TEST
  app_service_init_status = app_uart4_echo_init();
#elif APP_ENABLE_AP6212_BT_LDC_BRIDGE
  app_service_init_status = app_ap6212_bridge_init();
#elif APP_ENABLE_AP6212_BRINGUP
  app_service_init_status = app_uart4_console_init();
  if(app_service_init_status == TX_SUCCESS)
  {
    (void)app_uart4_console_write_string("\r\n[boot] ap6212 bringup mode\r\n");
    bsp_ap6212_power_on();
    (void)app_uart4_console_write_string("[ap6212] power pins set\r\n");
    app_service_init_status = app_ap6212_sdio_probe_init();
#if APP_ENABLE_AP6212_NETXDUO
    if(app_service_init_status == TX_SUCCESS)
      app_service_init_status = app_netxduo_init();
#endif
  }
#else
  app_service_init_status = TX_SUCCESS;
#endif
  /* USER CODE END App_ThreadX_Init */

  return ret;
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN  Before_Kernel_Start */

  /* USER CODE END  Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN  Kernel_Start_Error */

  /* USER CODE END  Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */
void app_led_task_entry(ULONG thread_input)
{
    (void)thread_input;
    for(;;)
    {
        HAL_GPIO_TogglePin(LED_R_GPIO_Port,LED_R_Pin);
        tx_thread_sleep(app_service_init_status == TX_SUCCESS ? 1000U : 100U);
    }
}
/* USER CODE END 1 */
