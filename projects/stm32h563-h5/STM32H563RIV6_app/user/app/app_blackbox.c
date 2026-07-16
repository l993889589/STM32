/**
 * @file app_blackbox.c
 * @brief ThreadX-facing RTC and BSP_FLASH adapter for the persistent journal.
 */

#include "app_blackbox.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#include "bsp_irq_lock.h"
#include "bsp_reset.h"
#include "bsp_flash.h"
#include "osal_mutex.h"
#include "ota_layout.h"

#define APP_BLACKBOX_QUEUE_DEPTH          16U
#define APP_BLACKBOX_LOG_BATCH             8U
#define APP_BLACKBOX_DRAIN_PER_STEP         2U
#define APP_BLACKBOX_STORE_LOCK_MS      10000U
#define APP_BLACKBOX_SOURCE_SYSTEM          1U
#define APP_BLACKBOX_BOOT_CODE              1U
#define APP_BLACKBOX_FAULT_QUEUE_OVERFLOW   2U

typedef struct
{
    blackbox_store_event_t event;
} app_blackbox_queue_entry_t;

typedef struct
{
    blackbox_store_t store;
    osal_mutex_t store_mutex;
    app_blackbox_queue_entry_t queue[APP_BLACKBOX_QUEUE_DEPTH];
    app_log_record_t log_batch[APP_BLACKBOX_LOG_BATCH];
    app_blackbox_snapshot_t snapshot;
    uint32_t last_log_sequence;
    uint16_t queue_head;
    uint16_t queue_tail;
    uint16_t queue_count;
} app_blackbox_context_t;

static app_blackbox_context_t g_blackbox;

/** @brief Return true when a Gregorian year is a leap year. */
static bool app_blackbox_is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U) &&
           (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

/** @brief Return the number of days in one supported month. */
static uint8_t app_blackbox_days_in_month(uint16_t year, uint8_t month)
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
    return ((month == 2U) && app_blackbox_is_leap_year(year)) ?
           29U : days[month - 1U];
}

/** @brief Convert a validated RTC calendar to seconds since 2000-01-01. */
static uint32_t app_blackbox_datetime_to_seconds(
    const bsp_rtc_datetime_t *datetime)
{
    uint32_t days = 0U;
    uint16_t year;
    uint8_t month;

    for(year = 2000U; year < datetime->year; ++year)
    {
        days += app_blackbox_is_leap_year(year) ? 366U : 365U;
    }
    for(month = 1U; month < datetime->month; ++month)
    {
        days += app_blackbox_days_in_month(datetime->year, month);
    }
    days += (uint32_t)datetime->day - 1U;
    return (days * 86400UL) +
           ((uint32_t)datetime->hour * 3600UL) +
           ((uint32_t)datetime->minute * 60UL) +
           datetime->second;
}

/** @brief Derive RTC weekday where Monday is one and Sunday is seven. */
static uint8_t app_blackbox_weekday(uint16_t year,
                                    uint8_t month,
                                    uint8_t day)
{
    static const uint8_t offsets[12] =
        {0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U};
    uint32_t adjusted_year = year;
    uint32_t sunday_based;

    if(month < 3U)
    {
        --adjusted_year;
    }
    sunday_based = (adjusted_year + (adjusted_year / 4U) -
                    (adjusted_year / 100U) + (adjusted_year / 400U) +
                    offsets[month - 1U] + day) % 7U;
    return (sunday_based == 0U) ? 7U : (uint8_t)sunday_based;
}

/** @brief Parse one English three-letter build month. */
static uint8_t app_blackbox_parse_build_month(const char *month)
{
    static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    uint8_t index;

    for(index = 0U; index < 12U; ++index)
    {
        if(strncmp(month, &names[index * 3U], 3U) == 0)
        {
            return (uint8_t)(index + 1U);
        }
    }
    return 0U;
}

/** @brief Parse compiler build date/time into the RTC public structure. */
static bool app_blackbox_get_build_datetime(bsp_rtc_datetime_t *datetime)
{
    static const char build_date[] = __DATE__;
    static const char build_time[] = __TIME__;
    unsigned int day;
    unsigned int year;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    uint8_t month;

    if(datetime == NULL)
    {
        return false;
    }
    month = app_blackbox_parse_build_month(build_date);
    if((month == 0U) ||
       (sscanf(&build_date[4], "%u %u", &day, &year) != 2) ||
       (sscanf(build_time,
               "%u:%u:%u",
               &hour,
               &minute,
               &second) != 3) ||
       (year < 2000U) || (year > 2099U))
    {
        return false;
    }
    datetime->year = (uint16_t)year;
    datetime->month = month;
    datetime->day = (uint8_t)day;
    datetime->weekday = app_blackbox_weekday((uint16_t)year,
                                             month,
                                             (uint8_t)day);
    datetime->hour = (uint8_t)hour;
    datetime->minute = (uint8_t)minute;
    datetime->second = (uint8_t)second;
    return true;
}

/** @brief Seed an invalid RTC from build time without resetting backup state. */
static bsp_status_t app_blackbox_prepare_rtc(void)
{
    bsp_rtc_datetime_t build_datetime;
    bool is_valid = false;
    bsp_status_t status = bsp_rtc_init();

    if((status != BSP_STATUS_OK) &&
       (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return status;
    }
    status = bsp_rtc_is_datetime_valid(&is_valid);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    if(is_valid)
    {
        g_blackbox.snapshot.rtc_is_valid = 1U;
        return BSP_STATUS_OK;
    }
    if(!app_blackbox_get_build_datetime(&build_datetime))
    {
        return BSP_STATUS_NOT_READY;
    }
    status = bsp_rtc_set_datetime(&build_datetime);
    if(status == BSP_STATUS_OK)
    {
        g_blackbox.snapshot.rtc_is_valid = 1U;
        g_blackbox.snapshot.rtc_was_build_seeded = 1U;
    }
    return status;
}

/** @brief Bind one journal read to the serialized GD25 driver. */
static bool app_blackbox_flash_read(uint32_t address,
                                    uint8_t *data,
                                    uint32_t size,
                                    void *context)
{
    (void)context;
    return bsp_flash_read(address, data, size);
}

/** @brief Bind one journal sector erase to the serialized GD25 driver. */
static bool app_blackbox_flash_erase(uint32_t address, void *context)
{
    (void)context;
    return bsp_flash_erase_4k(address);
}

/** @brief Bind one journal program fragment to the serialized GD25 driver. */
static bool app_blackbox_flash_program(uint32_t address,
                                       const uint8_t *data,
                                       uint32_t size,
                                       void *context)
{
    (void)context;
    return bsp_flash_write(address, data, size);
}

/** @brief Fill current RTC and monotonic timestamps into an event. */
static void app_blackbox_timestamp_event(blackbox_store_event_t *event,
                                         uint32_t uptime_ms)
{
    bsp_rtc_datetime_t datetime;

    event->uptime_ms = uptime_ms;
    if(bsp_rtc_get_datetime(&datetime) == BSP_STATUS_OK)
    {
        event->rtc_seconds_2000 =
            app_blackbox_datetime_to_seconds(&datetime);
        event->flags |= APP_BLACKBOX_FLAG_RTC_VALID;
        if(g_blackbox.snapshot.rtc_was_build_seeded != 0U)
        {
            event->flags |= APP_BLACKBOX_FLAG_RTC_BUILD_SEEDED;
        }
    }
}

/** @brief Persist one event while serializing journal metadata ownership. */
static bool app_blackbox_write_event(blackbox_store_event_t *event,
                                     uint32_t now_ms)
{
    bool result;

    if(event->uptime_ms == 0U)
    {
        app_blackbox_timestamp_event(event, now_ms);
    }
    if(!osal_mutex_lock(&g_blackbox.store_mutex,
                        APP_BLACKBOX_STORE_LOCK_MS))
    {
        ++g_blackbox.snapshot.failed_events;
        return false;
    }
    result = blackbox_store_append(&g_blackbox.store, event);
    if(result)
    {
        ++g_blackbox.snapshot.written_events;
        blackbox_store_get_stats(&g_blackbox.store,
                                 &g_blackbox.snapshot.store);
        g_blackbox.snapshot.last_record.sequence =
            g_blackbox.snapshot.store.newest_sequence;
        g_blackbox.snapshot.last_record.event = *event;
    }
    else
    {
        ++g_blackbox.snapshot.failed_events;
    }
    osal_mutex_unlock(&g_blackbox.store_mutex);
    return result;
}

/** @brief Remove one queued event under a save/restore interrupt lock. */
static bool app_blackbox_queue_pop(blackbox_store_event_t *event)
{
    bsp_irq_state_t irq_state;
    bool has_event = false;

    irq_state = bsp_irq_lock();
    if(g_blackbox.queue_count > 0U)
    {
        *event = g_blackbox.queue[g_blackbox.queue_tail].event;
        g_blackbox.queue_tail = (uint16_t)(
            (g_blackbox.queue_tail + 1U) % APP_BLACKBOX_QUEUE_DEPTH);
        --g_blackbox.queue_count;
        has_event = true;
    }
    g_blackbox.snapshot.queued_events = g_blackbox.queue_count;
    bsp_irq_unlock(irq_state);
    return has_event;
}

/** @brief Mirror new warning and error RAM log records into the journal queue. */
static void app_blackbox_capture_logs(void)
{
    uint16_t count = app_log_read_tail(g_blackbox.log_batch,
                                       APP_BLACKBOX_LOG_BATCH);
    uint16_t index;

    for(index = 0U; index < count; ++index)
    {
        const app_log_record_t *record = &g_blackbox.log_batch[index];
        uint16_t length;

        if((int32_t)(record->sequence - g_blackbox.last_log_sequence) <= 0)
        {
            continue;
        }
        g_blackbox.last_log_sequence = record->sequence;
        if(record->level > APP_LOG_LEVEL_WARN)
        {
            continue;
        }
        length = (uint16_t)strlen(record->message);
        if(length > BLACKBOX_STORE_PAYLOAD_SIZE)
        {
            length = BLACKBOX_STORE_PAYLOAD_SIZE;
        }
        (void)app_blackbox_record(APP_BLACKBOX_EVENT_LOG,
                                  (record->level == APP_LOG_LEVEL_ERROR) ?
                                  APP_BLACKBOX_SEVERITY_ERROR :
                                  APP_BLACKBOX_SEVERITY_WARNING,
                                  (uint16_t)record->module,
                                  0U,
                                  record->message,
                                  length);
    }
}

/** @brief Initialize RTC, recover the journal, and append one boot record. */
bsp_status_t app_blackbox_init(void)
{
    const blackbox_store_io_t io =
    {
        .read = app_blackbox_flash_read,
        .erase_sector = app_blackbox_flash_erase,
        .program = app_blackbox_flash_program,
        .context = NULL
    };
    const blackbox_store_config_t config =
    {
        .base_address = BLACKBOX_EXT_FLASH_ADDR,
        .size_bytes = BLACKBOX_EXT_FLASH_SIZE,
        .sector_size_bytes = BLACKBOX_EXT_SECTOR_SIZE
    };
    blackbox_store_event_t event;
    bsp_status_t rtc_status;

    if(g_blackbox.snapshot.is_initialized != 0U)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }
    (void)memset(&g_blackbox, 0, sizeof(g_blackbox));
    if(!osal_mutex_init(&g_blackbox.store_mutex, "blackbox_store"))
    {
        return BSP_STATUS_IO_ERROR;
    }

    rtc_status = app_blackbox_prepare_rtc();
    g_blackbox.snapshot.rtc_status = rtc_status;
    if(!blackbox_store_init(&g_blackbox.store, &io, &config))
    {
        return BSP_STATUS_IO_ERROR;
    }
    (void)bsp_reset_get_causes(&g_blackbox.snapshot.reset_causes);

    (void)memset(&event, 0, sizeof(event));
    event.type = APP_BLACKBOX_EVENT_BOOT;
    event.severity = APP_BLACKBOX_SEVERITY_INFO;
    event.source = APP_BLACKBOX_SOURCE_SYSTEM;
    event.code = APP_BLACKBOX_BOOT_CODE;
    {
        int payload_length = snprintf(
            (char *)event.payload,
            sizeof(event.payload),
            "reset=0x%08lX",
            (unsigned long)g_blackbox.snapshot.reset_causes);

        event.payload_length = (payload_length > 0) ?
            (uint16_t)payload_length : 0U;
    }
    if(event.payload_length >= (uint16_t)sizeof(event.payload))
    {
        event.payload_length = sizeof(event.payload) - 1U;
    }
    app_blackbox_timestamp_event(&event, 0U);
    g_blackbox.snapshot.is_initialized = 1U;
    if(!app_blackbox_write_event(&event, 0U))
    {
        return BSP_STATUS_IO_ERROR;
    }
    bsp_reset_clear_causes();
    blackbox_store_get_stats(&g_blackbox.store, &g_blackbox.snapshot.store);
    return BSP_STATUS_OK;
}

/** @brief Drain a bounded number of queued events and warning/error logs. */
void app_blackbox_step(uint32_t now_ms)
{
    blackbox_store_event_t event;
    uint32_t count;

    if(g_blackbox.snapshot.is_initialized == 0U)
    {
        return;
    }
    app_blackbox_capture_logs();
    for(count = 0U; count < APP_BLACKBOX_DRAIN_PER_STEP; ++count)
    {
        if(!app_blackbox_queue_pop(&event))
        {
            break;
        }
        (void)app_blackbox_write_event(&event, now_ms);
    }
}

/** @brief Queue a persistent event without performing SPI operations. */
bool app_blackbox_record(app_blackbox_event_type_t type,
                         app_blackbox_severity_t severity,
                         uint16_t source,
                         uint16_t code,
                         const void *payload,
                         uint16_t payload_length)
{
    blackbox_store_event_t *event;
    bsp_irq_state_t irq_state;

    if((g_blackbox.snapshot.is_initialized == 0U) ||
       (payload_length > BLACKBOX_STORE_PAYLOAD_SIZE) ||
       ((payload_length > 0U) && (payload == NULL)))
    {
        return false;
    }

    irq_state = bsp_irq_lock();
    if(g_blackbox.queue_count >= APP_BLACKBOX_QUEUE_DEPTH)
    {
        ++g_blackbox.snapshot.dropped_events;
        bsp_irq_unlock(irq_state);
        return false;
    }
    event = &g_blackbox.queue[g_blackbox.queue_head].event;
    (void)memset(event, 0, sizeof(*event));
    event->type = (uint8_t)type;
    event->severity = (uint8_t)severity;
    event->source = source;
    event->code = code;
    event->payload_length = payload_length;
    if(payload_length > 0U)
    {
        (void)memcpy(event->payload, payload, payload_length);
    }
    g_blackbox.queue_head = (uint16_t)(
        (g_blackbox.queue_head + 1U) % APP_BLACKBOX_QUEUE_DEPTH);
    ++g_blackbox.queue_count;
    g_blackbox.snapshot.queued_events = g_blackbox.queue_count;
    bsp_irq_unlock(irq_state);
    return true;
}

/** @brief Read the newest persisted records in chronological order. */
uint16_t app_blackbox_read_tail(blackbox_store_record_t *records,
                                uint16_t max_records)
{
    uint16_t count = 0U;

    if((g_blackbox.snapshot.is_initialized == 0U) ||
       !osal_mutex_lock(&g_blackbox.store_mutex,
                        APP_BLACKBOX_STORE_LOCK_MS))
    {
        return 0U;
    }
    count = blackbox_store_read_tail(&g_blackbox.store,
                                     records,
                                     max_records);
    osal_mutex_unlock(&g_blackbox.store_mutex);
    return count;
}

/** @brief Start a fresh logical journal generation without mass erasing. */
bsp_status_t app_blackbox_clear(void)
{
    bool result;

    if(g_blackbox.snapshot.is_initialized == 0U)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(!osal_mutex_lock(&g_blackbox.store_mutex,
                        APP_BLACKBOX_STORE_LOCK_MS))
    {
        return BSP_STATUS_BUSY;
    }
    result = blackbox_store_clear(&g_blackbox.store);
    if(result)
    {
        (void)memset(&g_blackbox.snapshot.last_record,
                     0,
                     sizeof(g_blackbox.snapshot.last_record));
    }
    blackbox_store_get_stats(&g_blackbox.store, &g_blackbox.snapshot.store);
    osal_mutex_unlock(&g_blackbox.store_mutex);
    return result ? BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Copy one coherent service and storage health snapshot. */
void app_blackbox_get_snapshot(app_blackbox_snapshot_t *snapshot)
{
    bsp_irq_state_t irq_state;

    if(snapshot == NULL)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    *snapshot = g_blackbox.snapshot;
    bsp_irq_unlock(irq_state);
}

/** @brief Read the retained RTC calendar through the black-box RTC owner. */
bsp_status_t app_blackbox_get_datetime(bsp_rtc_datetime_t *datetime)
{
    return bsp_rtc_get_datetime(datetime);
}

/** @brief Set the retained RTC calendar and calculate an omitted weekday. */
bsp_status_t app_blackbox_set_datetime(const bsp_rtc_datetime_t *datetime)
{
    bsp_rtc_datetime_t normalized;
    bsp_status_t status;

    if(datetime == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    normalized = *datetime;
    if((normalized.weekday < 1U) || (normalized.weekday > 7U))
    {
        if((normalized.month < 1U) || (normalized.month > 12U) ||
           (normalized.day < 1U) ||
           (normalized.day > app_blackbox_days_in_month(normalized.year,
                                                        normalized.month)))
        {
            return BSP_STATUS_INVALID_ARGUMENT;
        }
        normalized.weekday = app_blackbox_weekday(normalized.year,
                                                  normalized.month,
                                                  normalized.day);
    }
    status = bsp_rtc_set_datetime(&normalized);

    if(status == BSP_STATUS_OK)
    {
        g_blackbox.snapshot.rtc_is_valid = 1U;
        g_blackbox.snapshot.rtc_was_build_seeded = 0U;
        g_blackbox.snapshot.rtc_status = BSP_STATUS_OK;
    }
    return status;
}

/** @brief Convert seconds since 2000-01-01 into a Gregorian calendar. */
bool app_blackbox_seconds_to_datetime(uint32_t seconds,
                                      bsp_rtc_datetime_t *datetime)
{
    uint32_t days;
    uint32_t seconds_in_day;
    uint16_t year = 2000U;
    uint8_t month = 1U;

    if(datetime == NULL)
    {
        return false;
    }
    days = seconds / 86400UL;
    seconds_in_day = seconds % 86400UL;
    while(year <= 2099U)
    {
        uint32_t days_in_year = app_blackbox_is_leap_year(year) ? 366U : 365U;

        if(days < days_in_year)
        {
            break;
        }
        days -= days_in_year;
        ++year;
    }
    if(year > 2099U)
    {
        return false;
    }
    while(month <= 12U)
    {
        uint8_t month_days = app_blackbox_days_in_month(year, month);

        if(days < month_days)
        {
            break;
        }
        days -= month_days;
        ++month;
    }
    datetime->year = year;
    datetime->month = month;
    datetime->day = (uint8_t)(days + 1U);
    datetime->weekday = app_blackbox_weekday(year,
                                             month,
                                             datetime->day);
    datetime->hour = (uint8_t)(seconds_in_day / 3600UL);
    seconds_in_day %= 3600UL;
    datetime->minute = (uint8_t)(seconds_in_day / 60UL);
    datetime->second = (uint8_t)(seconds_in_day % 60UL);
    return true;
}
