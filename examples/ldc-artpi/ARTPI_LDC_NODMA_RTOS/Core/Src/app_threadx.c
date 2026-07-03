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
#include "ldc_uart4_test.h"

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
static TX_THREAD ldc_uart4_thread;
static UCHAR ldc_uart4_thread_stack[1024];
static TX_SEMAPHORE ldc_uart4_rx_sem;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static void ldc_uart4_thread_entry(ULONG input);
static void ldc_uart4_notify(ldc_easy_event_t event, void *arg);
static void ldc_uart4_app_packet(const uint8_t *data, uint16_t len, void *arg);

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

  /* USER CODE END App_ThreadX_MEM_POOL */

  /* USER CODE BEGIN App_ThreadX_Init */
  ret = tx_semaphore_create(&ldc_uart4_rx_sem, "ldc uart4 rx", 0U);
  if(ret == TX_SUCCESS)
  {
    ret = tx_thread_create(&ldc_uart4_thread,
                           "ldc uart4 test",
                           ldc_uart4_thread_entry,
                           0U,
                           ldc_uart4_thread_stack,
                           sizeof(ldc_uart4_thread_stack),
                           10U,
                           10U,
                           TX_NO_TIME_SLICE,
                           TX_AUTO_START);
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
  /* USER CODE BEGIN  Before_Kernel_Start */

  /* USER CODE END  Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN  Kernel_Start_Error */

  /* USER CODE END  Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */
static void ldc_uart4_thread_entry(ULONG input)
{
  (void)input;

  ldc_uart4_test_set_event_handler(ldc_uart4_notify, NULL);
  ldc_uart4_test_set_packet_handler(ldc_uart4_app_packet, NULL);
  ldc_uart4_test_init();

  for (;;)
  {
    if(tx_semaphore_get(&ldc_uart4_rx_sem, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
      ldc_uart4_test_poll();
      while(tx_semaphore_get(&ldc_uart4_rx_sem, TX_NO_WAIT) == TX_SUCCESS)
      {
        ldc_uart4_test_poll();
      }
    }
  }
}

static void ldc_uart4_notify(ldc_easy_event_t event, void *arg)
{
  (void)arg;

  if(event == LDC_EASY_EVT_PACKET)
  {
    (void)tx_semaphore_put(&ldc_uart4_rx_sem);
  }
}

static void ldc_uart4_app_packet(const uint8_t *data, uint16_t len, void *arg)
{
  (void)arg;

  ldc_uart4_test_write_text("APP RX ");
  ldc_uart4_test_write_u32((uint32_t)len);
  ldc_uart4_test_write_text(" bytes: ");
  ldc_uart4_test_write(data, len);
  if(len == 0U || data[len - 1U] != '\n')
  {
    ldc_uart4_test_write_text("\r\n");
  }
}

/* USER CODE END 1 */
