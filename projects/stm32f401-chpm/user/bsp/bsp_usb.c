/**
 * @file bsp_usb.c
 * @brief PA11/PA12 USB OTG FS device ownership for STM32F401CC.
 */

#include "bsp_usb.h"

#include <stdbool.h>

#include "stm32f4xx_hal.h"

#define BSP_USB_IRQ_PRIORITY (7U)

static PCD_HandleTypeDef g_usb_pcd;
static bool g_usb_initialized;

/** @brief Initialize USB pins, peripheral clock, IRQ, and PCD state. */
bsp_status_t bsp_usb_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if(g_usb_initialized)
        return BSP_STATUS_ALREADY_INITIALIZED;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_usb_pcd.Instance = USB_OTG_FS;
    g_usb_pcd.Init.dev_endpoints = 4U;
    g_usb_pcd.Init.speed = PCD_SPEED_FULL;
    g_usb_pcd.Init.dma_enable = DISABLE;
    g_usb_pcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    g_usb_pcd.Init.Sof_enable = DISABLE;
    g_usb_pcd.Init.low_power_enable = DISABLE;
    g_usb_pcd.Init.lpm_enable = DISABLE;
    g_usb_pcd.Init.vbus_sensing_enable = DISABLE;
    g_usb_pcd.Init.use_dedicated_ep1 = DISABLE;
    if(HAL_PCD_Init(&g_usb_pcd) != HAL_OK)
        return BSP_STATUS_IO_ERROR;

    HAL_NVIC_SetPriority(OTG_FS_IRQn, BSP_USB_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    g_usb_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Return the opaque PCD context consumed only by the USBX adapter. */
uintptr_t bsp_usb_get_dcd_context(void)
{
    return g_usb_initialized ? (uintptr_t)&g_usb_pcd : (uintptr_t)0U;
}

/** @brief Apply proven FIFO allocation and start device signaling. */
bsp_status_t bsp_usb_start(void)
{
    if(!g_usb_initialized)
        return BSP_STATUS_NOT_READY;
    if(HAL_PCDEx_SetRxFiFo(&g_usb_pcd, 128U) != HAL_OK ||
       HAL_PCDEx_SetTxFiFo(&g_usb_pcd, 0U, 64U) != HAL_OK ||
       HAL_PCDEx_SetTxFiFo(&g_usb_pcd, 1U, 128U) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    return (HAL_PCD_Start(&g_usb_pcd) == HAL_OK) ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Own the USB FS vector next to the private PCD handle. */
void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&g_usb_pcd);
}
