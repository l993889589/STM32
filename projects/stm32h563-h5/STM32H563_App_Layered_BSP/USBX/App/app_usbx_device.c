/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_usbx_device.c
  * @author  MCD Application Team
  * @brief   USBX Device applicative file
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
#include "app_usbx_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_board_io.h"
#include "board_usb.h"
#include "main.h"
#include "usb_vendor_transport.h"
#include "ux_device_class_dpump.h"
#include "ux_system.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_USB_VENDOR_BULK_ENABLE 0U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

static ULONG cdc_acm_interface_number;
static ULONG cdc_acm_configuration_number;
static UX_SLAVE_CLASS_CDC_ACM_PARAMETER cdc_acm_parameter;
#if APP_USB_VENDOR_BULK_ENABLE
static ULONG vendor_interface_number;
static ULONG vendor_configuration_number;
static UX_SLAVE_CLASS_DPUMP_PARAMETER vendor_parameter;
#endif
static TX_THREAD ux_device_app_thread;
#if APP_USB_VENDOR_BULK_ENABLE
static TX_THREAD ux_vendor_app_thread;
#endif

/* USER CODE BEGIN PV */
static UCHAR g_cdc_rx_buffer[256];
#if APP_USB_VENDOR_BULK_ENABLE
static UCHAR g_vendor_rx_buffer[USB_VENDOR_MAX_FRAME];
#endif

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static VOID app_ux_device_thread_entry(ULONG thread_input);
#if APP_USB_VENDOR_BULK_ENABLE
static VOID app_ux_vendor_thread_entry(ULONG thread_input);
#endif
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/**
  * @brief  Application USBX Device Initialization.
  * @param  memory_ptr: memory pointer
  * @retval status
  */

UINT app_usbx_device_init(VOID *memory_ptr)
{
   UINT ret = UX_SUCCESS;
  UCHAR *device_framework_high_speed;
  UCHAR *device_framework_full_speed;
  ULONG device_framework_hs_length;
  ULONG device_framework_fs_length;
  ULONG string_framework_length;
  ULONG language_id_framework_length;
  UCHAR *string_framework;
  UCHAR *language_id_framework;

  UCHAR *pointer;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN app_usbx_device_init_0 */

  /* USER CODE END app_usbx_device_init_0 */
  /* Allocate the stack for USBX Memory */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer,
                       USBX_DEVICE_MEMORY_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_ALLOCATE_STACK_ERROR */
    return TX_POOL_ERROR;
    /* USER CODE END USBX_ALLOCATE_STACK_ERROR */
  }

  /* Initialize USBX Memory */
  if (ux_system_initialize(pointer, USBX_DEVICE_MEMORY_STACK_SIZE, UX_NULL, 0) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_SYSTEM_INITIALIZE_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_SYSTEM_INITIALIZE_ERROR */
  }

  /* Get Device Framework High Speed and get the length */
  device_framework_high_speed = USBD_Get_Device_Framework_Speed(USBD_HIGH_SPEED,
                                                                &device_framework_hs_length);

  /* Get Device Framework Full Speed and get the length */
  device_framework_full_speed = USBD_Get_Device_Framework_Speed(USBD_FULL_SPEED,
                                                                &device_framework_fs_length);

  /* Get String Framework and get the length */
  string_framework = USBD_Get_String_Framework(&string_framework_length);

  /* Get Language Id Framework and get the length */
  language_id_framework = USBD_Get_Language_Id_Framework(&language_id_framework_length);

  /* Install the device portion of USBX */
  if (ux_device_stack_initialize(device_framework_high_speed,
                                 device_framework_hs_length,
                                 device_framework_full_speed,
                                 device_framework_fs_length,
                                 string_framework,
                                 string_framework_length,
                                 language_id_framework,
                                 language_id_framework_length,
                                 UX_NULL) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_DEVICE_INITIALIZE_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_DEVICE_INITIALIZE_ERROR */
  }

  /* Initialize the cdc acm class parameters for the device */
  cdc_acm_parameter.ux_slave_class_cdc_acm_instance_activate   = USBD_CDC_ACM_Activate;
  cdc_acm_parameter.ux_slave_class_cdc_acm_instance_deactivate = USBD_CDC_ACM_Deactivate;
  cdc_acm_parameter.ux_slave_class_cdc_acm_parameter_change    = USBD_CDC_ACM_ParameterChange;

  /* USER CODE BEGIN CDC_ACM_PARAMETER */

  /* USER CODE END CDC_ACM_PARAMETER */

  /* Get cdc acm configuration number */
  cdc_acm_configuration_number = USBD_Get_Configuration_Number(CLASS_TYPE_CDC_ACM, 0);

  /* Find cdc acm interface number */
  cdc_acm_interface_number = USBD_Get_Interface_Number(CLASS_TYPE_CDC_ACM, 0);

  /* Initialize the device cdc acm class */
  if (ux_device_stack_class_register(_ux_system_slave_class_cdc_acm_name,
                                     ux_device_class_cdc_acm_entry,
                                     cdc_acm_configuration_number,
                                     cdc_acm_interface_number,
                                     &cdc_acm_parameter) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_DEVICE_CDC_ACM_REGISTER_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_DEVICE_CDC_ACM_REGISTER_ERROR */
  }

#if APP_USB_VENDOR_BULK_ENABLE
  /* Register the vendor DPUMP class used by the LDV1 framed binary channel. */
  vendor_parameter.ux_slave_class_dpump_instance_activate = usb_vendor_transport_activate;
  vendor_parameter.ux_slave_class_dpump_instance_deactivate = usb_vendor_transport_deactivate;
  vendor_configuration_number = USBD_Get_Configuration_Number(CLASS_TYPE_VENDOR, 0);
  vendor_interface_number = USBD_Get_Interface_Number(CLASS_TYPE_VENDOR, 0);
  if (ux_device_stack_class_register(_ux_system_slave_class_dpump_name,
                                     ux_device_class_dpump_entry,
                                     vendor_configuration_number,
                                     vendor_interface_number,
                                     &vendor_parameter) != UX_SUCCESS)
  {
    /* USER CODE BEGIN USBX_DEVICE_VENDOR_REGISTER_ERROR */
    return UX_ERROR;
    /* USER CODE END USBX_DEVICE_VENDOR_REGISTER_ERROR */
  }
#endif

  /* Allocate the stack for device application main thread */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, UX_DEVICE_APP_THREAD_STACK_SIZE,
                       TX_NO_WAIT) != TX_SUCCESS)
  {
    /* USER CODE BEGIN MAIN_THREAD_ALLOCATE_STACK_ERROR */
    return TX_POOL_ERROR;
    /* USER CODE END MAIN_THREAD_ALLOCATE_STACK_ERROR */
  }

  /* Create the device application main thread */
  if (tx_thread_create(&ux_device_app_thread, UX_DEVICE_APP_THREAD_NAME, app_ux_device_thread_entry,
                       0, pointer, UX_DEVICE_APP_THREAD_STACK_SIZE, UX_DEVICE_APP_THREAD_PRIO,
                       UX_DEVICE_APP_THREAD_PREEMPTION_THRESHOLD, UX_DEVICE_APP_THREAD_TIME_SLICE,
                       UX_DEVICE_APP_THREAD_START_OPTION) != TX_SUCCESS)
  {
    /* USER CODE BEGIN MAIN_THREAD_CREATE_ERROR */
    return TX_THREAD_ERROR;
    /* USER CODE END MAIN_THREAD_CREATE_ERROR */
  }

#if APP_USB_VENDOR_BULK_ENABLE
  /* Allocate the stack for the vendor bulk reader thread. */
  if (tx_byte_allocate(byte_pool, (VOID **) &pointer, 1536U, TX_NO_WAIT) != TX_SUCCESS)
  {
    /* USER CODE BEGIN VENDOR_THREAD_ALLOCATE_STACK_ERROR */
    return TX_POOL_ERROR;
    /* USER CODE END VENDOR_THREAD_ALLOCATE_STACK_ERROR */
  }

  if (tx_thread_create(&ux_vendor_app_thread, "USBX Vendor Bulk Thread",
                       app_ux_vendor_thread_entry, 0, pointer, 1536U,
                       UX_DEVICE_APP_THREAD_PRIO, UX_DEVICE_APP_THREAD_PRIO,
                       TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
  {
    /* USER CODE BEGIN VENDOR_THREAD_CREATE_ERROR */
    return TX_THREAD_ERROR;
    /* USER CODE END VENDOR_THREAD_CREATE_ERROR */
  }
#endif

  /* USER CODE BEGIN app_usbx_device_init_1 */

  /* USER CODE END app_usbx_device_init_1 */

  return ret;
}

/**
  * @brief  Function implementing app_ux_device_thread_entry.
  * @param  thread_input: User thread input parameter.
  * @retval none
  */
static VOID app_ux_device_thread_entry(ULONG thread_input)
{
  /* USER CODE BEGIN app_ux_device_thread_entry */
  ULONG actual_length;
  PCD_HandleTypeDef *pcd;
  bsp_status_t usb_status;

  TX_PARAMETER_NOT_USED(thread_input);

  usb_status = board_usb_init();
  if((usb_status != BSP_STATUS_OK) &&
     (usb_status != BSP_STATUS_ALREADY_INITIALIZED))
  {
    return;
  }
  pcd = board_usb_get_handle();
  if(pcd == NULL)
  {
    return;
  }

  if(ux_dcd_stm32_initialize(0U, (ULONG)pcd) != UX_SUCCESS)
  {
    return;
  }

  if(board_usb_start() != BSP_STATUS_OK)
  {
    return;
  }

  for(;;)
  {
    UX_SLAVE_CLASS_CDC_ACM *cdc_acm = app_usb_cdc_get();

    if(cdc_acm == UX_NULL)
    {
      tx_thread_sleep(10U);
      continue;
    }

    app_usb_cdc_service();

    if(ux_device_class_cdc_acm_read(cdc_acm, g_cdc_rx_buffer, sizeof(g_cdc_rx_buffer), &actual_length) == UX_SUCCESS &&
       actual_length != 0U)
    {
      app_usb_cdc_process_rx(g_cdc_rx_buffer, actual_length);
    }
    else
    {
      tx_thread_sleep(1U);
    }
  }
  /* USER CODE END app_ux_device_thread_entry */
}

#if APP_USB_VENDOR_BULK_ENABLE
/**
  * @brief  Blocking reader for the vendor Bulk interface.
  * @param  thread_input: unused ThreadX entry value.
  * @retval none
  *
  * This thread owns EP3 OUT reception. It feeds raw USB transactions into the
  * LDV1 stream parser; channel dispatch and responses are handled by
  * app_usb_service through usb_vendor_transport.
  */
static VOID app_ux_vendor_thread_entry(ULONG thread_input)
{
  /* USER CODE BEGIN app_ux_vendor_thread_entry */
  uint32_t actual_length;

  TX_PARAMETER_NOT_USED(thread_input);

  for(;;)
  {
    if(!usb_vendor_transport_is_connected())
    {
      tx_thread_sleep(10U);
      continue;
    }

    if(usb_vendor_transport_read(g_vendor_rx_buffer,
                                 sizeof(g_vendor_rx_buffer),
                                 &actual_length) == UX_SUCCESS &&
       actual_length != 0U)
    {
      usb_vendor_transport_feed(g_vendor_rx_buffer, actual_length);
    }
    else
    {
      tx_thread_sleep(1U);
    }
  }
  /* USER CODE END app_ux_vendor_thread_entry */
}
#endif

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
