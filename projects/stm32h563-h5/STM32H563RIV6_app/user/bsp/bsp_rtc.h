/**
 * @file bsp_rtc.h
 * @brief Battery-backed board real-time clock public interface.
 */

#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} bsp_rtc_datetime_t;

/**
 * Initialize the RTC from the board's 32.768 kHz LSE crystal.
 * @return BSP status; conflict means another RTC clock source owns the backup domain.
 */
bsp_status_t bsp_rtc_init(void);
/**
 * Store a validated calendar value and mark backup time as valid.
 * @param datetime Gregorian date in 2000..2099 and weekday in 1..7.
 * @return BSP status.
 */
bsp_status_t bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime);
/**
 * Read a coherent calendar snapshot.
 * @param datetime Receives date and time in binary form.
 * @return BSP status; not-ready means the application has never set a valid time.
 */
bsp_status_t bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime);
/**
 * Query whether the backup-domain validity marker is present.
 * @param is_valid Receives the validity state.
 * @return BSP status.
 */
bsp_status_t bsp_rtc_is_datetime_valid(bool *is_valid);

/**
 * @brief Arm a one-shot RTC wakeup interrupt in whole seconds.
 * @param seconds Delay from 1 through 65536 seconds.
 * @return BSP status.
 */
bsp_status_t bsp_rtc_schedule_wakeup(uint32_t seconds);

/** @brief Cancel the RTC wakeup timer and its interrupt. */
bsp_status_t bsp_rtc_cancel_wakeup(void);

/**
 * @brief Consume the RTC wake-event latch set by the interrupt handler.
 * @return True exactly once for each observed RTC wake interrupt.
 */
bool bsp_rtc_take_wakeup_event(void);

#endif
