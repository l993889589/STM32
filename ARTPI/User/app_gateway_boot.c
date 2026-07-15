#include "app_gateway_boot.h"

#include "stm32h7xx_hal.h"
#include "bsp_qspi_w25q128.h"
#include "gateway_ota_format.h"

#define APP_GATEWAY_BOOT_IMAGE_MAGIC    0x50374148UL

extern uint8_t Load$$RW_IRAM1$$Limit;

void app_gateway_boot_publish_image(uint32_t image_version)
{
#if defined(ART_PI_QSPI_APP)
    uint32_t image_size =
        (uint32_t)(uintptr_t)&Load$$RW_IRAM1$$Limit -
        BSP_QSPI_W25Q128_BASE_ADDRESS;

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_CLK_ENABLE();
    RTC->BKP1R = image_size;
    RTC->BKP2R = image_version;
    RTC->BKP0R = APP_GATEWAY_BOOT_IMAGE_MAGIC;
#else
    (void)image_version;
#endif
}

void app_gateway_boot_mark_healthy(void)
{
#if defined(ART_PI_QSPI_APP)
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_RTC_CLK_ENABLE();
    if(RTC->BKP6R == GATEWAY_OTA_TRIAL_MAGIC)
    {
        RTC->BKP3R = GATEWAY_OTA_HEALTH_MAGIC;
        __DSB();
        NVIC_SystemReset();
    }
#endif
}
