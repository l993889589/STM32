/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_board_io.h"
#include "app_ota.h"
#include "app_nearlink.h"

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
static TX_THREAD app_rs485_thread;
static TX_THREAD app_led_thread;
static TX_THREAD app_w800_thread;
static TX_THREAD app_ota_confirm_thread;
static TX_THREAD app_nearlink_thread;
static UCHAR app_rs485_thread_stack[1024];
static UCHAR app_led_thread_stack[512];
static UCHAR app_w800_thread_stack[4096];
static UCHAR app_ota_confirm_thread_stack[2048];
static UCHAR app_nearlink_thread_stack[3072];

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
  if(tx_thread_create(&app_led_thread, "LED Blink",
                      app_led_task_entry, 0U,
                      app_led_thread_stack, sizeof(app_led_thread_stack),
                      20U, 20U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  ret = app_board_io_init();
  if(ret != TX_SUCCESS)
  {
    return ret;
  }

  if(tx_thread_create(&app_rs485_thread, "RS485 Modbus Slave",
                      app_rs485_task_entry, 0U,
                      app_rs485_thread_stack, sizeof(app_rs485_thread_stack),
                      12U, 12U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_w800_thread, "W800 WiFi MQTT",
                      app_w800_task_entry, 0U,
                      app_w800_thread_stack, sizeof(app_w800_thread_stack),
                      14U, 14U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_nearlink_thread, "NearLink AT",
                      app_nearlink_task_entry, 0U,
                      app_nearlink_thread_stack, sizeof(app_nearlink_thread_stack),
                      13U, 13U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_ota_confirm_thread, "OTA Confirm",
                      app_ota_confirm_task_entry, 0U,
                      app_ota_confirm_thread_stack, sizeof(app_ota_confirm_thread_stack),
                      22U, 22U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }
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
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
