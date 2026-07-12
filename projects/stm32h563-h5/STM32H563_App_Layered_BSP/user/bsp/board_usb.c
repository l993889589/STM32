/**
 * @file board_usb.c
 * @brief STM32H563 USB FS clock, PCD, PMA, IRQ, and lifecycle ownership.
 */

#include "board_usb.h"

#include <stddef.h>

#include "board_resources.h"

static PCD_HandleTypeDef g_usb_pcd;
static uint8_t g_usb_initialized;

/** @brief Configure one endpoint in the static USB packet memory layout. */
static bsp_status_t board_usb_configure_pma(uint32_t endpoint,
                                            uint32_t address)
{
    return HAL_PCDEx_PMAConfig(&g_usb_pcd,
                               endpoint,
                               PCD_SNG_BUF,
                               address) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Implement board_usb_init() without CubeMX-generated usb.c. */
bsp_status_t board_usb_init(void)
{
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};

    if(g_usb_initialized != 0U)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USB;
    peripheral_clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    HAL_PWREx_EnableVddUSB();
    __HAL_RCC_USB_CLK_ENABLE();

    g_usb_pcd.Instance = USB_DRD_FS;
    g_usb_pcd.Init.dev_endpoints = 8U;
    g_usb_pcd.Init.speed = USBD_FS_SPEED;
    g_usb_pcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    g_usb_pcd.Init.Sof_enable = DISABLE;
    g_usb_pcd.Init.low_power_enable = DISABLE;
    g_usb_pcd.Init.lpm_enable = DISABLE;
    g_usb_pcd.Init.battery_charging_enable = DISABLE;
    g_usb_pcd.Init.vbus_sensing_enable = DISABLE;
    g_usb_pcd.Init.bulk_doublebuffer_enable = ENABLE;
    g_usb_pcd.Init.iso_singlebuffer_enable = ENABLE;
    if(HAL_PCD_Init(&g_usb_pcd) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    if((board_usb_configure_pma(0x00U, 0x040U) != BSP_STATUS_OK) ||
       (board_usb_configure_pma(0x80U, 0x080U) != BSP_STATUS_OK) ||
       (board_usb_configure_pma(0x01U, 0x0C0U) != BSP_STATUS_OK) ||
       (board_usb_configure_pma(0x81U, 0x100U) != BSP_STATUS_OK) ||
       (board_usb_configure_pma(0x82U, 0x140U) != BSP_STATUS_OK) ||
       (board_usb_configure_pma(0x03U, 0x180U) != BSP_STATUS_OK) ||
       (board_usb_configure_pma(0x83U, 0x1C0U) != BSP_STATUS_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }

    HAL_NVIC_SetPriority(BOARD_USB_IRQ, BOARD_USB_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(BOARD_USB_IRQ);
    g_usb_initialized = 1U;
    return BSP_STATUS_OK;
}

/** @brief Implement board_usb_start() after USBX DCD initialization. */
bsp_status_t board_usb_start(void)
{
    if(g_usb_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_PCD_Start(&g_usb_pcd) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Stop USB signaling so host traffic cannot abort system Stop entry. */
bsp_status_t board_usb_prepare_stop(void)
{
    if(g_usb_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    HAL_NVIC_DisableIRQ(BOARD_USB_IRQ);
    HAL_NVIC_ClearPendingIRQ(BOARD_USB_IRQ);
    return HAL_PCD_Stop(&g_usb_pcd) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Restore USB IRQ routing and reconnect after system clock recovery. */
bsp_status_t board_usb_resume_after_stop(void)
{
    if(g_usb_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    HAL_NVIC_ClearPendingIRQ(BOARD_USB_IRQ);
    HAL_NVIC_SetPriority(BOARD_USB_IRQ, BOARD_USB_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(BOARD_USB_IRQ);
    return HAL_PCD_Start(&g_usb_pcd) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Return the private PCD handle to the USBX DCD adapter. */
PCD_HandleTypeDef *board_usb_get_handle(void)
{
    return g_usb_initialized != 0U ? &g_usb_pcd : NULL;
}

/** @brief Dispatch the USB FS vector to the owned PCD handle. */
void board_usb_irq_from_isr(void)
{
    if(g_usb_initialized != 0U)
    {
        HAL_PCD_IRQHandler(&g_usb_pcd);
    }
}
