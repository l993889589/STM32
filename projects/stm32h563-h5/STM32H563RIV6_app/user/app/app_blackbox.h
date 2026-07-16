/**
 * @file app_blackbox.h
 * @brief RTC-stamped persistent event black box for application services.
 */

#ifndef APP_BLACKBOX_H
#define APP_BLACKBOX_H

#include <stdbool.h>
#include <stdint.h>

#include "blackbox_store.h"
#include "bsp_rtc.h"
#include "bsp_status.h"

#define APP_BLACKBOX_FLAG_RTC_VALID        (1U << 0)
#define APP_BLACKBOX_FLAG_RTC_BUILD_SEEDED (1U << 1)

typedef enum
{
    APP_BLACKBOX_EVENT_BOOT = 1,
    APP_BLACKBOX_EVENT_LOG = 2,
    APP_BLACKBOX_EVENT_FAULT = 3,
    APP_BLACKBOX_EVENT_SELF_TEST = 4,
    APP_BLACKBOX_EVENT_POWER = 5,
    APP_BLACKBOX_EVENT_MANUAL = 6
} app_blackbox_event_type_t;

typedef enum
{
    APP_BLACKBOX_SEVERITY_ERROR = 0,
    APP_BLACKBOX_SEVERITY_WARNING = 1,
    APP_BLACKBOX_SEVERITY_INFO = 2
} app_blackbox_severity_t;

typedef struct
{
    blackbox_store_stats_t store;
    blackbox_store_record_t last_record;
    bsp_status_t rtc_status;
    uint32_t reset_causes;
    uint32_t queued_events;
    uint32_t dropped_events;
    uint32_t written_events;
    uint32_t failed_events;
    uint8_t rtc_is_valid;
    uint8_t rtc_was_build_seeded;
    uint8_t is_initialized;
} app_blackbox_snapshot_t;

/**
 * @brief Initialize RTC, recover the journal, and append one boot record.
 * @return BSP status; storage failure is reported as IO_ERROR.
 */
bsp_status_t app_blackbox_init(void);

/**
 * @brief Drain a bounded number of queued events and mirror warning/error logs.
 * @param now_ms Monotonic milliseconds used when a queued event omitted uptime.
 */
void app_blackbox_step(uint32_t now_ms);

/**
 * @brief Queue a persistent event without performing SPI operations.
 * @param type Stable event category.
 * @param severity Error, warning, or informational severity.
 * @param source Application-defined source identifier.
 * @param code Application-defined event code.
 * @param payload Optional bytes copied immediately; maximum is 28 bytes.
 * @param payload_length Number of payload bytes.
 * @return true when the bounded RAM queue accepts the event.
 */
bool app_blackbox_record(app_blackbox_event_type_t type,
                         app_blackbox_severity_t severity,
                         uint16_t source,
                         uint16_t code,
                         const void *payload,
                         uint16_t payload_length);

/**
 * @brief Read the newest persisted records in chronological order.
 * @param records Caller-owned output array.
 * @param max_records Maximum entries, limited to 32.
 * @return Number of records returned.
 */
uint16_t app_blackbox_read_tail(blackbox_store_record_t *records,
                                uint16_t max_records);

/** @brief Start a fresh logical journal generation without mass erasing Flash. */
bsp_status_t app_blackbox_clear(void);

/** @brief Copy one coherent service and storage health snapshot. */
void app_blackbox_get_snapshot(app_blackbox_snapshot_t *snapshot);

/** @brief Read the retained RTC calendar through the black-box RTC owner. */
bsp_status_t app_blackbox_get_datetime(bsp_rtc_datetime_t *datetime);

/** @brief Set the retained RTC calendar; weekday zero is calculated automatically. */
bsp_status_t app_blackbox_set_datetime(const bsp_rtc_datetime_t *datetime);

/**
 * @brief Convert seconds since 2000-01-01 into a Gregorian calendar.
 * @param seconds Seconds since the black-box epoch.
 * @param datetime Destination calendar; weekday uses 1=Monday through 7=Sunday.
 * @return true when the value fits the supported 2000..2099 RTC range.
 */
bool app_blackbox_seconds_to_datetime(uint32_t seconds,
                                      bsp_rtc_datetime_t *datetime);

#endif /* APP_BLACKBOX_H */
