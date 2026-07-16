/**
 * @file app_power.c
 * @brief ThreadX-owned Stop entry, RTC/touch/W800 wake, restore, and logging.
 */

#include "app_power.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_blackbox.h"
#include "app_config.h"
#include "app_self_test.h"
#include "app_w800.h"
#include "bsp_clock.h"
#include "bsp.h"
#include "bsp_irq_lock.h"
#include "bsp_power.h"
#include "bsp_pwm.h"
#include "bsp_rtc.h"
#include "bsp_uart.h"

#define APP_POWER_SOURCE_ID              3U
#define APP_POWER_EVENT_ENTER            1U
#define APP_POWER_EVENT_WAKE             2U
#define APP_POWER_W800_PAUSE_TIMEOUT_MS 5000U
#define APP_POWER_BLACKBOX_DRAIN_MS      200U
#define APP_POWER_POST_WAKE_TEST_MS     5000U
#define APP_POWER_AUTO_MIN_RESIDENCY_MS 2000U

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND       1000U
#endif

typedef struct
{
    uint32_t generation;
    uint32_t requested_seconds;
    uint32_t armed_sources;
    uint32_t wake_reason;
    uint32_t measured_seconds;
    int32_t error;
} app_power_blackbox_payload_t;

typedef struct
{
    app_power_snapshot_t snapshot;
    uint32_t pending_seconds;
    uint32_t pending_sources;
    uint32_t auto_wake_sources;
    uint32_t idle_since_ms;
    uint32_t deadlines_ms[APP_POWER_OWNER_COUNT];
    uint8_t lock_counts[APP_POWER_OWNER_COUNT];
    volatile uint8_t request_pending;
} app_power_context_t;

static app_power_context_t g_app_power;

/** @brief Return the wrap-safe ThreadX time base in milliseconds. */
static uint32_t app_power_now_ms(void)
{
    uint64_t ticks = tx_time_get();

    return (uint32_t)((ticks * 1000ULL) /
                      (uint64_t)TX_TIMER_TICKS_PER_SECOND);
}

/** @brief Return true when a 32-bit millisecond deadline has elapsed. */
static bool app_power_deadline_elapsed(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/** @brief Refresh the nearest relative deadline in the public snapshot. */
static void app_power_refresh_next_deadline(uint32_t now_ms)
{
    uint32_t nearest_ms = 0U;
    uint32_t index;

    for(index = 0U; index < (uint32_t)APP_POWER_OWNER_COUNT; ++index)
    {
        uint32_t bit = 1UL << index;
        uint32_t remaining_ms;

        if((g_app_power.snapshot.deadline_mask & bit) == 0U)
        {
            continue;
        }
        if(app_power_deadline_elapsed(now_ms,
                                      g_app_power.deadlines_ms[index]))
        {
            remaining_ms = 0U;
        }
        else
        {
            remaining_ms = g_app_power.deadlines_ms[index] - now_ms;
        }
        if((nearest_ms == 0U) || (remaining_ms < nearest_ms))
        {
            nearest_ms = remaining_ms;
        }
    }
    g_app_power.snapshot.next_deadline_ms = nearest_ms;
}

/** @brief Convert one RTC calendar to a monotonic second count for deltas. */
static uint32_t app_power_datetime_seconds(const bsp_rtc_datetime_t *datetime)
{
    static const uint16_t days_before_month[12] =
        {0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U};
    uint32_t year = datetime->year;
    uint32_t days = (year - 2000U) * 365U + ((year - 2001U) / 4U);

    days += days_before_month[datetime->month - 1U];
    if((datetime->month > 2U) && ((year % 4U) == 0U))
    {
        days++;
    }
    days += (uint32_t)datetime->day - 1U;
    return (days * 86400UL) + ((uint32_t)datetime->hour * 3600UL) +
           ((uint32_t)datetime->minute * 60UL) + datetime->second;
}

/** @brief Persist one compact enter or wake transaction record. */
static void app_power_record(uint16_t code,
                             app_blackbox_severity_t severity,
                             const app_power_blackbox_payload_t *payload)
{
    (void)app_blackbox_record(APP_BLACKBOX_EVENT_POWER,
                              severity,
                              APP_POWER_SOURCE_ID,
                              code,
                              payload,
                              (uint16_t)sizeof(*payload));
}

/** @brief Atomically publish a new power execution state and error. */
static void app_power_set_state(app_power_state_t state, int32_t error)
{
    bsp_irq_state_t irq_state = bsp_irq_lock();

    g_app_power.snapshot.state = state;
    g_app_power.snapshot.last_error = error;
    bsp_irq_unlock(irq_state);
}

/** @brief Restore backlight settings captured before Stop entry. */
static bool app_power_restore_backlight(const bsp_pwm_result_t *saved,
                                        bool was_running)
{
    bsp_pwm_config_t config;

    if(!was_running)
    {
        return true;
    }
    config.frequency_hz = saved->requested_frequency_hz;
    config.duty_permille = saved->requested_duty_permille;
    return (bsp_pwm_configure(BOARD_PWM_LCD_BACKLIGHT, &config, NULL) ==
            BSP_STATUS_OK) &&
           (bsp_pwm_start(BOARD_PWM_LCD_BACKLIGHT) == BSP_STATUS_OK);
}

/** @brief Execute one fully serialized Stop and wake transaction. */
static void app_power_execute(uint32_t seconds, uint32_t requested_sources)
{
    app_power_blackbox_payload_t payload = {0};
    bsp_rtc_datetime_t before = {0};
    bsp_rtc_datetime_t after = {0};
    bsp_pwm_result_t backlight = {0};
    TX_THREAD *current_thread;
    UINT old_priority = 0U;
    bool priority_changed = false;
    bool w800_paused = false;
    bool w800_armed = false;
    bool touch_armed = false;
    bool rtc_armed = false;
    bool backlight_saved = false;
    bool usb_stopped = false;
    bool irq_masked = false;
    bool restore_ok = true;
    uint32_t wake_reason = 0U;
    uint32_t armed_sources = APP_POWER_WAKE_SOURCE_RTC;
    int32_t error = 0;

    app_power_set_state(APP_POWER_STATE_PREPARING, 0);
    if((requested_sources & APP_POWER_WAKE_SOURCE_TOUCH) != 0U)
    {
        armed_sources |= APP_POWER_WAKE_SOURCE_TOUCH;
    }
    if((requested_sources & APP_POWER_WAKE_SOURCE_W800) != 0U)
    {
        armed_sources |= APP_POWER_WAKE_SOURCE_W800;
    }
    g_app_power.snapshot.armed_sources = armed_sources;

    payload.generation = g_app_power.snapshot.generation;
    payload.requested_seconds = seconds;
    payload.armed_sources = armed_sources;
    app_power_record(APP_POWER_EVENT_ENTER, APP_BLACKBOX_SEVERITY_INFO, &payload);
    tx_thread_sleep(APP_POWER_BLACKBOX_DRAIN_MS);

    if((armed_sources & APP_POWER_WAKE_SOURCE_W800) != 0U)
    {
        w800_paused = app_w800_pause(APP_POWER_W800_PAUSE_TIMEOUT_MS);
        if(!w800_paused)
        {
            error = -10;
            goto cleanup;
        }
    }

    current_thread = tx_thread_identify();
    if((current_thread == NULL) ||
       (tx_thread_priority_change(current_thread, 0U, &old_priority) != TX_SUCCESS))
    {
        error = -11;
        goto cleanup;
    }
    priority_changed = true;

    backlight_saved = bsp_pwm_get_result(BOARD_PWM_LCD_BACKLIGHT,
                                         &backlight) == BSP_STATUS_OK;
    if(backlight_saved)
    {
        (void)bsp_pwm_stop(BOARD_PWM_LCD_BACKLIGHT);
    }
    usb_stopped = bsp_power_prepare_usb() == BSP_STATUS_OK;
    if(!usb_stopped)
    {
        error = -12;
        goto cleanup;
    }
    if(bsp_rtc_get_datetime(&before) != BSP_STATUS_OK)
    {
        error = -13;
        goto cleanup;
    }
    if(bsp_rtc_schedule_wakeup(seconds) != BSP_STATUS_OK)
    {
        error = -14;
        goto cleanup;
    }
    rtc_armed = true;

    if((armed_sources & APP_POWER_WAKE_SOURCE_TOUCH) != 0U)
    {
        if(bsp_power_configure_touch_wakeup(true) != BSP_STATUS_OK)
        {
            error = -15;
            goto cleanup;
        }
        touch_armed = true;
    }
    if((armed_sources & APP_POWER_WAKE_SOURCE_W800) != 0U)
    {
#if APP_POWER_AUTO_W800_TRIGGER_TEST
        bsp_w800_reset_assert();
        bsp_power_delay_ms(20U);
#endif
        if(bsp_uart_prepare_stop_wakeup(BSP_UART_W800_AT) != 0)
        {
            error = -16;
            goto cleanup;
        }
        w800_armed = true;
#if APP_POWER_AUTO_W800_TRIGGER_TEST
        bsp_w800_reset_release();
#endif
    }
    if(bsp_power_mask_non_wakeup_interrupts(true,
                                            touch_armed,
                                            w800_armed) != BSP_STATUS_OK)
    {
        error = -17;
        goto cleanup;
    }
    irq_masked = true;

    app_power_set_state(APP_POWER_STATE_SLEEPING, 0);
    bsp_power_suspend_tick();
    bsp_power_enter_stop();

    app_power_set_state(APP_POWER_STATE_RESTORING, 0);
    if(bsp_clock_configure_system() != BSP_STATUS_OK)
    {
        error = -18;
        restore_ok = false;
    }
    bsp_power_resume_tick();

cleanup:
    if(rtc_armed && bsp_rtc_take_wakeup_event())
    {
        wake_reason |= APP_POWER_WAKE_SOURCE_RTC;
    }
    if(touch_armed && bsp_power_take_touch_wakeup_event())
    {
        wake_reason |= APP_POWER_WAKE_SOURCE_TOUCH;
    }
    if(w800_armed)
    {
        if(bsp_uart_resume_after_stop(BSP_UART_W800_AT) != 0)
        {
            restore_ok = false;
        }
        if(bsp_uart_take_stop_wakeup_event(BSP_UART_W800_AT) != 0U)
        {
            wake_reason |= APP_POWER_WAKE_SOURCE_W800;
        }
    }
    if(irq_masked)
    {
        bsp_power_restore_interrupts();
    }
    if(touch_armed)
    {
        (void)bsp_power_configure_touch_wakeup(false);
    }
    if(rtc_armed)
    {
        (void)bsp_rtc_cancel_wakeup();
    }
    if(usb_stopped && bsp_power_resume_usb() != BSP_STATUS_OK)
    {
        restore_ok = false;
    }
    if(!app_power_restore_backlight(&backlight, backlight_saved))
    {
        restore_ok = false;
    }
    if(priority_changed)
    {
        UINT ignored_priority;
        (void)tx_thread_priority_change(current_thread,
                                        old_priority,
                                        &ignored_priority);
    }
    if(w800_paused)
    {
        app_w800_resume();
#if APP_POWER_AUTO_W800_TRIGGER_TEST
        app_w800_request_reconnect();
#endif
    }

    if((bsp_rtc_get_datetime(&after) == BSP_STATUS_OK) &&
       (after.month >= 1U) && (after.month <= 12U) &&
       (before.month >= 1U) && (before.month <= 12U))
    {
        uint32_t before_seconds = app_power_datetime_seconds(&before);
        uint32_t after_seconds = app_power_datetime_seconds(&after);
        g_app_power.snapshot.measured_sleep_seconds = after_seconds - before_seconds;
    }
    g_app_power.snapshot.wake_reason = wake_reason;
    g_app_power.snapshot.sleep_count++;
    if(!restore_ok)
    {
        g_app_power.snapshot.restore_error_count++;
        if(error == 0)
        {
            error = -19;
        }
    }

    payload.wake_reason = wake_reason;
    payload.measured_seconds = g_app_power.snapshot.measured_sleep_seconds;
    payload.error = error;
    app_power_record(APP_POWER_EVENT_WAKE,
                     error == 0 ? APP_BLACKBOX_SEVERITY_INFO :
                                  APP_BLACKBOX_SEVERITY_ERROR,
                     &payload);
    app_power_set_state(error == 0 ? APP_POWER_STATE_COMPLETED :
                                     APP_POWER_STATE_FAILED,
                        error);
    if(error == 0)
    {
        tx_thread_sleep(APP_POWER_POST_WAKE_TEST_MS);
        app_self_test_request_run();
    }
}

/** @brief Initialize the static service state without scheduling sleep. */
void app_power_init(void)
{
    (void)memset(&g_app_power, 0, sizeof(g_app_power));
    g_app_power.snapshot.state = APP_POWER_STATE_IDLE;
    g_app_power.snapshot.auto_enabled = APP_POWER_AUTO_STOP_ENABLE != 0U;
    g_app_power.snapshot.auto_idle_ms = APP_POWER_AUTO_IDLE_MS;
    g_app_power.snapshot.auto_max_sleep_seconds =
        APP_POWER_AUTO_MAX_SLEEP_SECONDS;
    g_app_power.auto_wake_sources = APP_POWER_WAKE_SOURCE_RTC |
                                    APP_POWER_WAKE_SOURCE_TOUCH |
                                    APP_POWER_WAKE_SOURCE_W800;
    g_app_power.idle_since_ms = app_power_now_ms();
}

/** @brief Queue one controlled Stop request if the owner is currently idle. */
int app_power_request_stop(uint32_t seconds, uint32_t wake_sources)
{
    bsp_irq_state_t irq_state;

    if((seconds == 0U) || (seconds > 65536U) ||
       ((wake_sources & ~(uint32_t)(APP_POWER_WAKE_SOURCE_RTC |
                                    APP_POWER_WAKE_SOURCE_TOUCH |
                                    APP_POWER_WAKE_SOURCE_W800)) != 0U))
    {
        return -1;
    }
    irq_state = bsp_irq_lock();
    if(g_app_power.request_pending != 0U ||
       g_app_power.snapshot.state == APP_POWER_STATE_PREPARING ||
       g_app_power.snapshot.state == APP_POWER_STATE_SLEEPING ||
       g_app_power.snapshot.state == APP_POWER_STATE_RESTORING)
    {
        g_app_power.snapshot.rejected_count++;
        bsp_irq_unlock(irq_state);
        return -1;
    }
    g_app_power.pending_seconds = seconds;
    g_app_power.pending_sources = wake_sources;
    g_app_power.request_pending = 1U;
    g_app_power.snapshot.generation++;
    g_app_power.snapshot.requested_sleep_seconds = seconds;
    g_app_power.snapshot.requested_sources = wake_sources;
    g_app_power.snapshot.measured_sleep_seconds = 0U;
    g_app_power.snapshot.wake_reason = 0U;
    g_app_power.snapshot.state = APP_POWER_STATE_PENDING;
    g_app_power.snapshot.last_error = 0;
    bsp_irq_unlock(irq_state);
    return 0;
}

/** @brief Acquire one nested owner lock and restart the idle interval. */
void app_power_wake_lock_acquire(app_power_owner_t owner)
{
    bsp_irq_state_t irq_state;
    uint32_t owner_index = (uint32_t)owner;

    if(owner_index >= (uint32_t)APP_POWER_OWNER_COUNT)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    if(g_app_power.lock_counts[owner_index] != UINT8_MAX)
    {
        ++g_app_power.lock_counts[owner_index];
    }
    g_app_power.snapshot.active_lock_mask |= 1UL << owner_index;
    bsp_irq_unlock(irq_state);
}

/** @brief Release one nested owner lock without underflowing its count. */
void app_power_wake_lock_release(app_power_owner_t owner)
{
    bsp_irq_state_t irq_state;
    uint32_t owner_index = (uint32_t)owner;

    if(owner_index >= (uint32_t)APP_POWER_OWNER_COUNT)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    if(g_app_power.lock_counts[owner_index] != 0U)
    {
        --g_app_power.lock_counts[owner_index];
        if(g_app_power.lock_counts[owner_index] == 0U)
        {
            g_app_power.snapshot.active_lock_mask &= ~(1UL << owner_index);
        }
    }
    bsp_irq_unlock(irq_state);
}

/** @brief Publish or clear one owner deadline using the common time base. */
void app_power_set_deadline(app_power_owner_t owner, uint32_t delay_ms)
{
    bsp_irq_state_t irq_state;
    uint32_t owner_index = (uint32_t)owner;

    if(owner_index >= (uint32_t)APP_POWER_OWNER_COUNT)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    if(delay_ms == 0U)
    {
        g_app_power.snapshot.deadline_mask &= ~(1UL << owner_index);
        g_app_power.deadlines_ms[owner_index] = 0U;
    }
    else
    {
        g_app_power.deadlines_ms[owner_index] = app_power_now_ms() + delay_ms;
        g_app_power.snapshot.deadline_mask |= 1UL << owner_index;
    }
    app_power_refresh_next_deadline(app_power_now_ms());
    bsp_irq_unlock(irq_state);
}

/** @brief Restart automatic idle qualification after owner activity. */
void app_power_note_activity(app_power_owner_t owner)
{
    bsp_irq_state_t irq_state;

    if((uint32_t)owner >= (uint32_t)APP_POWER_OWNER_COUNT)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    g_app_power.idle_since_ms = app_power_now_ms();
    bsp_irq_unlock(irq_state);
}

/** @brief Apply a validated automatic Stop policy atomically. */
int app_power_configure_auto(uint8_t enabled,
                             uint32_t idle_ms,
                             uint32_t max_sleep_seconds,
                             uint32_t wake_sources)
{
    bsp_irq_state_t irq_state;
    uint32_t valid_sources = APP_POWER_WAKE_SOURCE_RTC |
                             APP_POWER_WAKE_SOURCE_TOUCH |
                             APP_POWER_WAKE_SOURCE_W800;

    if((idle_ms < 1000U) || (idle_ms > 3600000U) ||
       (max_sleep_seconds == 0U) || (max_sleep_seconds > 65536U) ||
       ((wake_sources & ~valid_sources) != 0U))
    {
        return -1;
    }
    irq_state = bsp_irq_lock();
    g_app_power.snapshot.auto_enabled = enabled != 0U;
    g_app_power.snapshot.auto_idle_ms = idle_ms;
    g_app_power.snapshot.auto_max_sleep_seconds = max_sleep_seconds;
    g_app_power.auto_wake_sources = wake_sources | APP_POWER_WAKE_SOURCE_RTC;
    g_app_power.idle_since_ms = app_power_now_ms();
    bsp_irq_unlock(irq_state);
    return 0;
}

/** @brief Try to start one automatic transaction after all gates pass. */
static bool app_power_try_auto_stop(void)
{
    uint32_t now_ms = app_power_now_ms();
    uint32_t sleep_window_ms;
    uint32_t sleep_seconds;
    uint32_t wake_sources;
    bsp_irq_state_t irq_state = bsp_irq_lock();

    app_power_refresh_next_deadline(now_ms);
    if((g_app_power.snapshot.auto_enabled == 0U) ||
       (g_app_power.snapshot.active_lock_mask != 0U) ||
       ((uint32_t)(now_ms - g_app_power.idle_since_ms) <
        g_app_power.snapshot.auto_idle_ms))
    {
        bsp_irq_unlock(irq_state);
        return false;
    }

    sleep_window_ms = g_app_power.snapshot.auto_max_sleep_seconds * 1000U;
    if(g_app_power.snapshot.deadline_mask != 0U)
    {
        sleep_window_ms = g_app_power.snapshot.next_deadline_ms;
    }
    if(sleep_window_ms < APP_POWER_AUTO_MIN_RESIDENCY_MS)
    {
        ++g_app_power.snapshot.auto_blocked_count;
        g_app_power.idle_since_ms = now_ms;
        bsp_irq_unlock(irq_state);
        return false;
    }

    sleep_seconds = sleep_window_ms / 1000U;
    if(sleep_seconds > g_app_power.snapshot.auto_max_sleep_seconds)
    {
        sleep_seconds = g_app_power.snapshot.auto_max_sleep_seconds;
    }
    wake_sources = g_app_power.auto_wake_sources;
    ++g_app_power.snapshot.auto_sleep_count;
    ++g_app_power.snapshot.generation;
    g_app_power.snapshot.requested_sleep_seconds = sleep_seconds;
    g_app_power.snapshot.requested_sources = wake_sources;
    g_app_power.snapshot.measured_sleep_seconds = 0U;
    g_app_power.snapshot.wake_reason = 0U;
    g_app_power.idle_since_ms = now_ms;
    bsp_irq_unlock(irq_state);

    app_power_execute(sleep_seconds, wake_sources);
    return true;
}

/** @brief Copy one interrupt-safe power transaction snapshot. */
void app_power_get_snapshot(app_power_snapshot_t *snapshot)
{
    bsp_irq_state_t irq_state;

    if(snapshot == NULL)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    app_power_refresh_next_deadline(app_power_now_ms());
    *snapshot = g_app_power.snapshot;
    bsp_irq_unlock(irq_state);
}

/** @brief Own every Stop transaction from one dedicated ThreadX thread. */
void app_power_task_entry(ULONG thread_input)
{
    (void)thread_input;
    app_power_init();
#if APP_POWER_AUTO_RTC_TEST
    tx_thread_sleep(15000U);
    (void)app_power_request_stop(10U,
                                 APP_POWER_WAKE_SOURCE_RTC |
                                 APP_POWER_WAKE_SOURCE_TOUCH |
                                 APP_POWER_WAKE_SOURCE_W800);
#endif
    for(;;)
    {
        uint32_t seconds = 0U;
        uint32_t sources = 0U;
        bsp_irq_state_t irq_state = bsp_irq_lock();

        if(g_app_power.request_pending != 0U)
        {
            seconds = g_app_power.pending_seconds;
            sources = g_app_power.pending_sources;
            g_app_power.request_pending = 0U;
        }
        bsp_irq_unlock(irq_state);
        if(seconds != 0U)
        {
            app_power_execute(seconds, sources);
        }
        else
        {
            (void)app_power_try_auto_stop();
        }
        tx_thread_sleep(20U);
    }
}

/** @brief Convert one wake-lock owner to a stable diagnostic name. */
const char *app_power_owner_name(app_power_owner_t owner)
{
    switch(owner)
    {
        case APP_POWER_OWNER_USB:       return "usb";
        case APP_POWER_OWNER_RS485:     return "rs485";
        case APP_POWER_OWNER_CAN:       return "can";
        case APP_POWER_OWNER_SELF_TEST: return "self_test";
        case APP_POWER_OWNER_W800:      return "w800";
        case APP_POWER_OWNER_BLACKBOX:  return "blackbox";
        case APP_POWER_OWNER_OTA:       return "ota";
        case APP_POWER_OWNER_USER:      return "user";
        default:                        return "unknown";
    }
}

/** @brief Return one stable lower_snake_case service-state name. */
const char *app_power_state_name(app_power_state_t state)
{
    switch(state)
    {
        case APP_POWER_STATE_IDLE:      return "idle";
        case APP_POWER_STATE_PENDING:   return "pending";
        case APP_POWER_STATE_PREPARING: return "preparing";
        case APP_POWER_STATE_SLEEPING:  return "sleeping";
        case APP_POWER_STATE_RESTORING: return "restoring";
        case APP_POWER_STATE_COMPLETED: return "completed";
        case APP_POWER_STATE_FAILED:    return "failed";
        default:                        return "unknown";
    }
}

/** @brief Format a wake-source bitset without heap allocation. */
void app_power_format_wake_reason(uint32_t reason, char *text, uint32_t size)
{
    uint32_t used = 0U;

    if((text == NULL) || (size == 0U))
    {
        return;
    }
    text[0] = '\0';
    if((reason & APP_POWER_WAKE_SOURCE_RTC) != 0U)
    {
        used += (uint32_t)snprintf(&text[used], size - used, "rtc");
    }
    if((reason & APP_POWER_WAKE_SOURCE_TOUCH) != 0U && used < size)
    {
        used += (uint32_t)snprintf(&text[used], size - used,
                                   "%stouch", used == 0U ? "" : "+");
    }
    if((reason & APP_POWER_WAKE_SOURCE_W800) != 0U && used < size)
    {
        used += (uint32_t)snprintf(&text[used], size - used,
                                   "%sw800", used == 0U ? "" : "+");
    }
    if((reason == 0U) && (size > 1U))
    {
        (void)snprintf(text, size, "other");
    }
}
