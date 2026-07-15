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
#include "app_blackbox.h"
#include "app_can_self_test.h"
#include "app_config.h"
#if APP_ENABLE_OTA_CONFIRM
#include "app_ota.h"
#endif
#include "app_health.h"
#include "app_power.h"
#include "app_production_test.h"
#include "app_rs485.h"
#include "app_self_test.h"
#include "app_ui.h"
#include "app_debug.h"
#include "bsp_timer.h"
#include "ldc_easy.h"
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
static TX_THREAD app_rs485_server_thread;
static TX_THREAD app_blackbox_thread;
static TX_THREAD app_self_test_thread;
static TX_THREAD app_power_thread;
static TX_THREAD app_can_self_test_thread;
static TX_THREAD app_led_thread;
static TX_THREAD app_w800_thread;
#if APP_ENABLE_OTA_CONFIRM
static TX_THREAD app_ota_confirm_thread;
#endif
static TX_THREAD app_ui_thread;
static TX_THREAD app_tick_thread;
static UCHAR app_rs485_thread_stack[2048];
/*
 * The Modbus server owns the signed OTA call chain. BEGIN performs external
 * slot erase and FINISH performs SHA-256/ECDSA verification, so the former
 * 2 KiB stack can overflow even though ordinary RTU requests are shallow.
 */
static UCHAR app_rs485_server_thread_stack[8192];
static UCHAR app_blackbox_thread_stack[4096];
static UCHAR app_self_test_thread_stack[2048];
static UCHAR app_power_thread_stack[3072];
static UCHAR app_can_self_test_thread_stack[1536];
static UCHAR app_led_thread_stack[512];
static UCHAR app_tick_thread_stack[512];
static UCHAR app_w800_thread_stack[8192];
#if APP_ENABLE_OTA_CONFIRM
/* Confirmation performs control-record and legacy manifest I/O after 60 s. */
static UCHAR app_ota_confirm_thread_stack[6144];
#endif
static UCHAR app_ui_thread_stack[6144];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/** @brief Advance application-owned LDC timeout state in task context. */
static void app_tick_task_entry(ULONG thread_input)
{
    (void)thread_input;
    for(;;)
    {
        ldc_easy_tick_all(1U);
        tx_thread_sleep(1U);
    }
}

static volatile TX_THREAD *g_stack_error_thread;

/** @brief Recover the persistent journal and drain its bounded event queue. */
static void app_blackbox_task_entry(ULONG thread_input)
{
    bsp_status_t status;

    (void)thread_input;
    status = app_blackbox_init();
    for(;;)
    {
        if((status == BSP_STATUS_OK) ||
           (status == BSP_STATUS_ALREADY_INITIALIZED))
        {
            app_blackbox_step(bsp_timer_get_ms());
        }
        tx_thread_sleep(20U);
    }
}

/** @brief Run the bounded whole-board automatic self-test state machine. */
static void app_self_test_task_entry(ULONG thread_input)
{
    bool lock_held = false;

    (void)thread_input;
    app_self_test_init(bsp_timer_get_ms());
    for(;;)
    {
        app_self_test_snapshot_t snapshot;

        app_self_test_step(bsp_timer_get_ms());
        app_production_test_step(bsp_timer_get_ms());
        app_self_test_get_snapshot(&snapshot);
        if((snapshot.state != APP_SELF_TEST_STATE_COMPLETED) && !lock_held)
        {
            app_power_wake_lock_acquire(APP_POWER_OWNER_SELF_TEST);
            lock_held = true;
        }
        else if((snapshot.state == APP_SELF_TEST_STATE_COMPLETED) && lock_held)
        {
            app_power_wake_lock_release(APP_POWER_OWNER_SELF_TEST);
            lock_held = false;
        }
        tx_thread_sleep(50U);
    }
}

/** @brief Run the bounded dual-FDCAN cross-link state machine. */
static void app_can_self_test_task_entry(ULONG thread_input)
{
    bsp_status_t status;
    bool lock_held = false;

    (void)thread_input;
    status = app_can_self_test_init();
    for(;;)
    {
        if((status == BSP_STATUS_OK) ||
           (status == BSP_STATUS_ALREADY_INITIALIZED))
        {
            app_can_self_test_step(bsp_timer_get_ms());
            app_can_self_test_snapshot_t snapshot;

            app_can_self_test_get_snapshot(&snapshot);
            if(((snapshot.state == APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST) ||
                (snapshot.state == APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE)) &&
               !lock_held)
            {
                app_power_wake_lock_acquire(APP_POWER_OWNER_CAN);
                lock_held = true;
            }
            else if((snapshot.state != APP_CAN_SELF_TEST_STATE_WAIT_CAN2_REQUEST) &&
                    (snapshot.state != APP_CAN_SELF_TEST_STATE_WAIT_CAN1_RESPONSE) &&
                    lock_held)
            {
                app_power_wake_lock_release(APP_POWER_OWNER_CAN);
                lock_held = false;
            }
        }
        tx_thread_sleep(5U);
    }
}

/** @brief Capture a ThreadX stack error and report it to application health. */
static void app_threadx_stack_error_handler(TX_THREAD *thread_ptr)
{
    g_stack_error_thread = thread_ptr;
    app_health_report_fault(APP_HEALTH_FAULT_THREAD_STACK);
    (void)thread_ptr;
}
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
  (void)tx_thread_stack_error_notify(app_threadx_stack_error_handler);
  app_health_init();
  if(tx_thread_create(&app_tick_thread, "LDC timeout tick",
                      app_tick_task_entry, 0U,
                      app_tick_thread_stack, sizeof(app_tick_thread_stack),
                      10U, 10U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }




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

  ret = app_debug_init();
  if(ret != TX_SUCCESS)
  {
    return ret;
  }

  if(tx_thread_create(&app_blackbox_thread, "RTC SPI NOR Blackbox",
                      app_blackbox_task_entry, 0U,
                      app_blackbox_thread_stack,
                      sizeof(app_blackbox_thread_stack),
                      19U, 19U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_self_test_thread, "Whole Board Self Test",
                      app_self_test_task_entry, 0U,
                      app_self_test_thread_stack,
                      sizeof(app_self_test_thread_stack),
                      17U, 17U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_power_thread, "Controlled Stop Power",
                      app_power_task_entry, 0U,
                      app_power_thread_stack,
                      sizeof(app_power_thread_stack),
                      6U, 6U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_can_self_test_thread, "Dual FDCAN Self Test",
                      app_can_self_test_task_entry, 0U,
                      app_can_self_test_thread_stack,
                      sizeof(app_can_self_test_thread_stack),
                      13U, 13U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_rs485_thread, "RS485 Modbus Master",
                      app_rs485_task_entry, 0U,
                      app_rs485_thread_stack, sizeof(app_rs485_thread_stack),
                      12U, 12U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

  if(tx_thread_create(&app_rs485_server_thread, "RS485 Modbus Server",
                      app_rs485_server_task_entry, 0U,
                      app_rs485_server_thread_stack,
                      sizeof(app_rs485_server_thread_stack),
                      11U, 11U,
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

  if(tx_thread_create(&app_ui_thread, "LVGL Dashboard",
                      app_ui_task_entry, 0U,
                      app_ui_thread_stack, sizeof(app_ui_thread_stack),
                      18U, 18U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }

#if APP_ENABLE_OTA_CONFIRM
  if(tx_thread_create(&app_ota_confirm_thread, "OTA Confirm",
                      app_ota_confirm_task_entry, 0U,
                      app_ota_confirm_thread_stack, sizeof(app_ota_confirm_thread_stack),
                      22U, 22U,
                      TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    return TX_THREAD_ERROR;
  }
#endif
  /* USER CODE END App_ThreadX_Init */

  return ret;
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */
void app_threadx_init(void)
{
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
