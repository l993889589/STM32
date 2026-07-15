/**
 * @file bsp_power.c
 * @brief Board-owned touch wake routing and STM32H563 Stop-mode boundary.
 */

#include "bsp_power.h"

#include "bsp_config.h"
#include "bsp_usb.h"
#include "stm32h5xx_hal.h"

static volatile bool bsp_touch_wakeup_event;
static uint32_t bsp_saved_nvic_enable[5];
static uint32_t bsp_saved_systick_control;
static bool bsp_nvic_mask_active;

/** @brief Forward ordered USB shutdown through its private board owner. */
bsp_status_t bsp_power_prepare_usb(void)
{
    return bsp_usb_prepare_stop();
}

/** @brief Forward ordered USB restart after the HSI48 clock is stable. */
bsp_status_t bsp_power_resume_usb(void)
{
    return bsp_usb_resume_after_stop();
}

/** @brief Configure PB14 as either falling-edge wake input or ordinary input. */
bsp_status_t bsp_power_configure_touch_wakeup(bool enable)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = BOARD_TOUCH_INTERRUPT_PIN;
    gpio.Mode = enable ? GPIO_MODE_IT_FALLING : GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_TOUCH_INTERRUPT_PORT, &gpio);

    bsp_touch_wakeup_event = false;
    __HAL_GPIO_EXTI_CLEAR_FLAG(BOARD_TOUCH_INTERRUPT_PIN);
    HAL_NVIC_ClearPendingIRQ(EXTI14_IRQn);
    if(enable)
    {
        HAL_NVIC_SetPriority(EXTI14_IRQn, 7U, 0U);
        HAL_NVIC_EnableIRQ(EXTI14_IRQn);
    }
    else
    {
        HAL_NVIC_DisableIRQ(EXTI14_IRQn);
    }
    return BSP_STATUS_OK;
}

/** @brief Atomically consume the EXTI-owned touch wake-event latch. */
bool bsp_power_take_touch_wakeup_event(void)
{
    uint32_t primask = __get_PRIMASK();
    bool event;

    __disable_irq();
    event = bsp_touch_wakeup_event;
    bsp_touch_wakeup_event = false;
    if(primask == 0U)
    {
        __enable_irq();
    }
    return event;
}

/** @brief Keep only explicitly selected wake vectors enabled during WFI. */
bsp_status_t bsp_power_mask_non_wakeup_interrupts(bool rtc_wakeup,
                                                  bool touch_wakeup,
                                                  bool w800_wakeup)
{
    uint32_t index;

    if(bsp_nvic_mask_active)
    {
        return BSP_STATUS_CONFLICT;
    }
    for(index = 0U; index < 5U; index++)
    {
        bsp_saved_nvic_enable[index] = NVIC->ISER[index];
        NVIC->ICER[index] = 0xFFFFFFFFUL;
    }
    bsp_saved_systick_control = SysTick->CTRL;
    SysTick->CTRL = bsp_saved_systick_control & ~SysTick_CTRL_TICKINT_Msk;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    __DSB();
    __ISB();
    if(rtc_wakeup)
    {
        NVIC_EnableIRQ(RTC_IRQn);
    }
    if(touch_wakeup)
    {
        NVIC_EnableIRQ(EXTI14_IRQn);
    }
    if(w800_wakeup)
    {
        NVIC_EnableIRQ(USART1_IRQn);
    }
    bsp_nvic_mask_active = true;
    return BSP_STATUS_OK;
}

/** @brief Restore only vectors that were enabled in the saved pre-Stop state. */
void bsp_power_restore_interrupts(void)
{
    uint32_t index;

    if(!bsp_nvic_mask_active)
    {
        return;
    }
    for(index = 0U; index < 5U; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ISER[index] = bsp_saved_nvic_enable[index];
    }
    SysTick->CTRL = bsp_saved_systick_control;
    __DSB();
    __ISB();
    bsp_nvic_mask_active = false;
}

/** @brief Enter STM32H563 Stop mode using the low-power regulator and WFI. */
void bsp_power_enter_stop(void)
{
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
}

/** @brief Route the dedicated EXTI14 vector to the touch interrupt owner. */
void EXTI14_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BOARD_TOUCH_INTERRUPT_PIN);
}

/** @brief Latch the touch falling edge for post-wake source classification. */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t gpio_pin)
{
    if(gpio_pin == BOARD_TOUCH_INTERRUPT_PIN)
    {
        bsp_touch_wakeup_event = true;
    }
}
