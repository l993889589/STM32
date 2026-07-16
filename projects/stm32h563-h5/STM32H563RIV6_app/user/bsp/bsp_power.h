/**
 * @file bsp_power.h
 * @brief STM32H563 Stop-mode entry and board wake-pin interface.
 */

#ifndef BSP_POWER_H
#define BSP_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

/**
 * @brief Configure or release the active-low touch interrupt as a wake source.
 * @param enable True to arm falling-edge EXTI14; false to return to plain input.
 * @return BSP status.
 */
bsp_status_t bsp_power_configure_touch_wakeup(bool enable);

/** @brief Disconnect and silence the board USB controller before Stop mode. */
bsp_status_t bsp_power_prepare_usb(void);

/** @brief Reconnect the board USB controller after the 48 MHz clock is restored. */
bsp_status_t bsp_power_resume_usb(void);

/** @brief Delay in the awake state using the board HAL timebase. */
void bsp_power_delay_ms(uint32_t delay_ms);

/** @brief Suspend the HAL timebase interrupt immediately before Stop entry. */
void bsp_power_suspend_tick(void);

/** @brief Resume the HAL timebase interrupt after the system clock is restored. */
void bsp_power_resume_tick(void);

/** @brief Consume one touch wake-event latch set by EXTI14. */
bool bsp_power_take_touch_wakeup_event(void);

/**
 * @brief Snapshot NVIC enables and mask every interrupt except selected wake IRQs.
 * @param rtc_wakeup Keep RTC_IRQn enabled.
 * @param touch_wakeup Keep EXTI14_IRQn enabled.
 * @param w800_wakeup Keep USART1_IRQn enabled.
 * @return BSP status; conflict means a mask is already active.
 */
bsp_status_t bsp_power_mask_non_wakeup_interrupts(bool rtc_wakeup,
                                                  bool touch_wakeup,
                                                  bool w800_wakeup);

/** @brief Restore the exact NVIC enable snapshot saved before Stop mode. */
void bsp_power_restore_interrupts(void);

/**
 * @brief Enter the real STM32H563 Stop mode until an enabled interrupt occurs.
 * @note The caller must restore the system clock immediately after return.
 */
void bsp_power_enter_stop(void);

#endif /* BSP_POWER_H */
