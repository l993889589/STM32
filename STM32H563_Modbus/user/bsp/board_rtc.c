/**
 * @file board_rtc.c
 * @brief STM32H563 RTC binding for the board's 32.768 kHz crystal.
 */

#include "bsp_rtc.h"

#include "stm32h5xx_hal.h"

#define BOARD_RTC_VALID_MARKER 0x48355254U

static RTC_HandleTypeDef board_rtc_handle;
static bool board_rtc_initialized;

/** @brief Return true when a Gregorian year is a leap year. */
static bool board_rtc_is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U) &&
           (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

/** @brief Return the number of days in a Gregorian month. */
static uint8_t board_rtc_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if((month == 0U) || (month > 12U))
    {
        return 0U;
    }
    return (month == 2U) && board_rtc_is_leap_year(year) ?
           29U : days[month - 1U];
}

/** @brief Validate the public RTC calendar domain before touching hardware. */
static bool board_rtc_datetime_is_valid(const bsp_rtc_datetime_t *datetime)
{
    uint8_t maximum_day;

    if((datetime == NULL) || (datetime->year < 2000U) ||
       (datetime->year > 2099U) || (datetime->weekday < 1U) ||
       (datetime->weekday > 7U) || (datetime->hour > 23U) ||
       (datetime->minute > 59U) || (datetime->second > 59U))
    {
        return false;
    }
    maximum_day = board_rtc_days_in_month(datetime->year, datetime->month);
    return (datetime->day >= 1U) && (datetime->day <= maximum_day);
}

/** @brief Enable LSE without silently erasing an existing backup domain. */
static bsp_status_t board_rtc_configure_clock(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    uint32_t selected_source;

    HAL_PWR_EnableBkUpAccess();
    selected_source = READ_BIT(RCC->BDCR, RCC_BDCR_RTCSEL);
    if((selected_source != 0U) &&
       (selected_source != RCC_RTCCLKSOURCE_LSE))
    {
        return BSP_STATUS_CONFLICT;
    }

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    oscillator.LSEState = RCC_LSE_ON;
    if(HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTC_CLK_ENABLE();
    return BSP_STATUS_OK;
}

/** @brief Initialize the board RTC while preserving retained calendar data. */
bsp_status_t bsp_rtc_init(void)
{
    bsp_status_t status;

    if(board_rtc_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }
    status = board_rtc_configure_clock();
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    board_rtc_handle.Instance = RTC;
    board_rtc_handle.Init.HourFormat = RTC_HOURFORMAT_24;
    board_rtc_handle.Init.AsynchPrediv = 127U;
    board_rtc_handle.Init.SynchPrediv = 255U;
    board_rtc_handle.Init.OutPut = RTC_OUTPUT_DISABLE;
    board_rtc_handle.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
    board_rtc_handle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    board_rtc_handle.Init.OutPutType = RTC_OUTPUT_TYPE_PUSHPULL;
    board_rtc_handle.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
    board_rtc_handle.Init.BinMode = RTC_BINARY_NONE;
    board_rtc_handle.Init.BinMixBcdU = RTC_BINARY_MIX_BCDU_0;
    if(HAL_RTC_Init(&board_rtc_handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    board_rtc_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Set the retained RTC calendar after validating its full domain. */
bsp_status_t bsp_rtc_set_datetime(const bsp_rtc_datetime_t *datetime)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    if(!board_rtc_datetime_is_valid(datetime))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_rtc_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    time.Hours = datetime->hour;
    time.Minutes = datetime->minute;
    time.Seconds = datetime->second;
    time.TimeFormat = RTC_HOURFORMAT12_AM;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    date.Year = (uint8_t)(datetime->year - 2000U);
    date.Month = datetime->month;
    date.Date = datetime->day;
    date.WeekDay = datetime->weekday;
    if((HAL_RTC_SetTime(&board_rtc_handle, &time, RTC_FORMAT_BIN) != HAL_OK) ||
       (HAL_RTC_SetDate(&board_rtc_handle, &date, RTC_FORMAT_BIN) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }
    HAL_RTCEx_BKUPWrite(&board_rtc_handle,
                        RTC_BKP_DR0,
                        BOARD_RTC_VALID_MARKER);
    return BSP_STATUS_OK;
}

/** @brief Read a coherent RTC calendar snapshot in binary form. */
bsp_status_t bsp_rtc_get_datetime(bsp_rtc_datetime_t *datetime)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    if(datetime == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_rtc_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(HAL_RTCEx_BKUPRead(&board_rtc_handle, RTC_BKP_DR0) !=
       BOARD_RTC_VALID_MARKER)
    {
        return BSP_STATUS_NOT_READY;
    }
    if((HAL_RTC_GetTime(&board_rtc_handle, &time, RTC_FORMAT_BIN) != HAL_OK) ||
       (HAL_RTC_GetDate(&board_rtc_handle, &date, RTC_FORMAT_BIN) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }
    datetime->year = (uint16_t)(2000U + date.Year);
    datetime->month = date.Month;
    datetime->day = date.Date;
    datetime->weekday = date.WeekDay;
    datetime->hour = time.Hours;
    datetime->minute = time.Minutes;
    datetime->second = time.Seconds;
    return BSP_STATUS_OK;
}

/** @brief Query the retained RTC validity marker. */
bsp_status_t bsp_rtc_is_datetime_valid(bool *is_valid)
{
    if(is_valid == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_rtc_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    *is_valid = HAL_RTCEx_BKUPRead(&board_rtc_handle, RTC_BKP_DR0) ==
                BOARD_RTC_VALID_MARKER;
    return BSP_STATUS_OK;
}
