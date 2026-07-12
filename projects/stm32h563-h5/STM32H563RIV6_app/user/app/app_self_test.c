/**
 * @file app_self_test.c
 * @brief Whole-board probes that separate absence, disconnection, and failure.
 */

#include "app_self_test.h"

#include <stdio.h>
#include <string.h>

#include "app_blackbox.h"
#include "app_can_self_test.h"
#include "app_health.h"
#include "app_log.h"
#include "app_rs485.h"
#include "app_w800.h"
#include "bsp_irq_lock.h"
#include "bsp_led.h"
#include "bsp_pwm.h"
#include "bsp_rtc.h"
#include "bsp_touch.h"
#include "bsp_uart.h"
#include "bsp_flash.h"
#include "usb_console.h"

#define APP_SELF_TEST_START_DELAY_MS 10000UL
#define APP_SELF_TEST_RTC_DELAY_MS    1200UL
#define APP_SELF_TEST_SOURCE_ID          2U

typedef struct
{
    app_self_test_snapshot_t snapshot;
    uint32_t start_deadline_ms;
    uint32_t rtc_deadline_ms;
    uint32_t rtc_reference_seconds;
    uint8_t current_item;
    uint8_t rtc_phase;
    volatile uint8_t run_requested;
} app_self_test_context_t;

static app_self_test_context_t g_self_test;

static const char *const g_self_test_item_names[APP_SELF_TEST_ITEM_COUNT] =
{
    "led",
    "lcd",
    "backlight_pwm",
    "touch",
    "spi_flash",
    "w800",
    "uart_w800",
    "uart_debug",
    "rs485_1",
    "rs485_2",
    "fdcan_1",
    "fdcan_2",
    "rtc",
    "usb",
    "blackbox"
};

static const char *const g_self_test_status_names[] =
{
    "not_run",
    "testing",
    "not_installed",
    "not_connected",
    "failed",
    "passed"
};

/** @brief Return true when a wrapping deadline has elapsed. */
static bool app_self_test_deadline_elapsed(uint32_t now_ms,
                                           uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

/** @brief Update one item and copy a bounded human-readable detail string. */
static void app_self_test_set_result(app_self_test_item_id_t id,
                                     app_self_test_status_t status,
                                     int32_t error_code,
                                     uint32_t value,
                                     const char *detail)
{
    app_self_test_item_t *item = &g_self_test.snapshot.items[id];

    item->status = status;
    item->error_code = error_code;
    item->value = value;
    (void)snprintf(item->detail,
                   sizeof(item->detail),
                   "%s",
                   (detail != NULL) ? detail : "");
}

/** @brief Convert a calendar snapshot into seconds for a ticking check. */
static bool app_self_test_rtc_seconds(uint32_t *seconds)
{
    bsp_rtc_datetime_t datetime;
    uint32_t day_count = 0U;
    uint16_t year;
    uint8_t month;
    static const uint8_t days[12] =
        {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    if((seconds == NULL) ||
       (app_blackbox_get_datetime(&datetime) != BSP_STATUS_OK))
    {
        return false;
    }
    for(year = 2000U; year < datetime.year; ++year)
    {
        day_count += ((year % 4U) == 0U) ? 366U : 365U;
    }
    for(month = 1U; month < datetime.month; ++month)
    {
        day_count += days[month - 1U];
        if((month == 2U) && ((datetime.year % 4U) == 0U))
        {
            ++day_count;
        }
    }
    day_count += datetime.day - 1U;
    *seconds = (day_count * 86400UL) +
               ((uint32_t)datetime.hour * 3600UL) +
               ((uint32_t)datetime.minute * 60UL) +
               datetime.second;
    return true;
}

/** @brief Verify the logical LED output can change and return to its state. */
static void app_self_test_led(void)
{
    uint8_t original = bsp_ledn_getstate(LED0);
    uint8_t changed;

    bsp_ledn_toggle(LED0);
    changed = bsp_ledn_getstate(LED0);
    if(changed == original)
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_LED,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 -1,
                                 changed,
                                 "logic did not toggle");
        return;
    }
    bsp_ledn_toggle(LED0);
    app_self_test_set_result(APP_SELF_TEST_ITEM_LED,
                             APP_SELF_TEST_STATUS_PASSED,
                             0,
                             original,
                             "gpio control path");
}

/** @brief Treat a fresh UI task heartbeat as LCD service success. */
static void app_self_test_lcd(void)
{
    app_health_status_t health;

    app_health_get_status(2000U, &health);
    if((health.seen_mask & (1UL << APP_HEALTH_SERVICE_UI)) != 0U &&
       (health.stale_mask & (1UL << APP_HEALTH_SERVICE_UI)) == 0U)
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_LCD,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 1U,
                                 "lvgl task active");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_LCD,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 -1,
                                 health.stale_mask,
                                 "lvgl heartbeat missing");
    }
}

/** @brief Verify the backlight timer retained its solved physical settings. */
static void app_self_test_pwm(void)
{
    bsp_pwm_result_t result;
    bsp_status_t status = bsp_pwm_get_result(BOARD_PWM_LCD_BACKLIGHT, &result);

    if((status == BSP_STATUS_OK) && (result.achieved_frequency_hz > 0U))
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_BACKLIGHT_PWM,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 result.achieved_frequency_hz,
                                 "timer configured");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_BACKLIGHT_PWM,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 status,
                                 0U,
                                 "timer unavailable");
    }
}

/** @brief Probe the touch controller identity without requiring a touch event. */
static void app_self_test_touch(void)
{
    bsp_touch_state_t state;
    int status = bsp_touch_read(&state);

    if((status == 0) && (state.present != 0U))
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_TOUCH,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 ((uint32_t)state.chip_id << 8U) |
                                     state.vendor_id,
                                 "controller identified");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_TOUCH,
                                 APP_SELF_TEST_STATUS_NOT_CONNECTED,
                                 status,
                                 0U,
                                 "no i2c response");
    }
}

/** @brief Probe the external NOR JEDEC capacity without erasing user data. */
static void app_self_test_flash(void)
{
    bsp_flash_id_t id;

    if(bsp_flash_read_id(&id) &&
       (id.manufacturer_id != 0x00U) &&
       (id.manufacturer_id != 0xFFU) &&
       (id.capacity == 0x18U))
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_SPI_FLASH,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 ((uint32_t)id.manufacturer_id << 16U) |
                                 ((uint32_t)id.memory_type << 8U) |
                                 id.capacity,
                                 "jedec 128 mbit");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_SPI_FLASH,
                                 APP_SELF_TEST_STATUS_NOT_CONNECTED,
                                 -1,
                                 0U,
                                 "jedec unavailable");
    }
}

/** @brief Classify W800 module presence independently from Wi-Fi connectivity. */
static void app_self_test_w800(void)
{
    app_w800_status_t status;
    bsp_uart_health_t uart;

    app_w800_get_status(&status);
    (void)memset(&uart, 0, sizeof(uart));
    (void)bsp_uart_get_health(BSP_UART_W800_AT, &uart);
    if((status.wifi_ready != 0U) || (uart.rx_bytes > 0U))
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_W800,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 uart.rx_bytes,
                                 status.wifi_ready ? "wifi ready" :
                                                     "at response seen");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_W800,
                                 APP_SELF_TEST_STATUS_NOT_CONNECTED,
                                 0,
                                 0U,
                                 "no at response");
    }
}

/** @brief Verify one logical UART is initialized and report DMA capability. */
static void app_self_test_uart(app_self_test_item_id_t item,
                               bsp_uart_port_t port)
{
    bsp_uart_health_t health;

    if(bsp_uart_get_health(port, &health) == 0)
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 health.rx_bytes,
                                 bsp_uart_rx_uses_dma(port) ?
                                     "uart dma ready" : "uart irq ready");
    }
    else
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 -1,
                                 0U,
                                 "uart not initialized");
    }
}

/** @brief Separate an initialized RS-485 port from a missing field peer. */
static void app_self_test_rs485(app_self_test_item_id_t item,
                                 bsp_uart_port_t port)
{
    bsp_uart_health_t health;
    app_rs485_loopback_snapshot_t loopback;

    (void)memset(&loopback, 0, sizeof(loopback));
    app_rs485_get_loopback_snapshot(&loopback);

    if(bsp_uart_get_health(port, &health) != 0)
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 -1,
                                 0U,
                                  "uart unavailable");
    }
    else if((loopback.server_crc_errors > 0U) ||
            (loopback.server_protocol_errors > 0U))
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 (int32_t)loopback.server_crc_errors,
                                 loopback.server_protocol_errors,
                                 "loopback frame errors");
    }
    else if((item == APP_SELF_TEST_ITEM_RS485_1) &&
            (loopback.master_passes > 0U))
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 loopback.master_passes,
                                 "master loopback passed");
    }
    else if((item == APP_SELF_TEST_ITEM_RS485_2) &&
            (loopback.server_responses > 0U))
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 loopback.server_responses,
                                 "server loopback passed");
    }
    else
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_NOT_CONNECTED,
                                 0,
                                 health.rx_bytes,
                                 health.rx_bytes > 0U ?
                                     "traffic without reply" :
                                     "no loopback response");
    }
}

/** @brief Classify each FDCAN channel from the shared cross-link test. */
static void app_self_test_fdcan(app_self_test_item_id_t item,
                                bool first_channel)
{
    app_can_self_test_snapshot_t can;
    uint32_t bus_off;
    uint8_t protocol_bus_off;

    app_can_self_test_get_snapshot(&can);
    bus_off = first_channel ? can.can1_bus_off_events :
                              can.can2_bus_off_events;
    protocol_bus_off = first_channel ? can.can1_protocol_bus_off :
                                       can.can2_protocol_bus_off;
    if((protocol_bus_off != 0U) || (can.recovery_failures > 0U))
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 (int32_t)can.last_status,
                                 bus_off,
                                 "cross-link unhealthy");
    }
    else if(can.passed_cycles > 0U)
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 can.passed_cycles,
                                 (bus_off > 0U || can.failed_cycles > 0U) ?
                                     "cross-link recovered" :
                                     "cross-link passed");
    }
    else
    {
        app_self_test_set_result(item,
                                 APP_SELF_TEST_STATUS_NOT_CONNECTED,
                                 0,
                                 0U,
                                 "no can response");
    }
}

/** @brief Verify RTC validity first, then verify the seconds counter advances. */
static bool app_self_test_rtc(uint32_t now_ms)
{
    uint32_t seconds;

    if(g_self_test.rtc_phase == 0U)
    {
        if(!app_self_test_rtc_seconds(&g_self_test.rtc_reference_seconds))
        {
            app_self_test_set_result(APP_SELF_TEST_ITEM_RTC,
                                     APP_SELF_TEST_STATUS_FAILED,
                                     -1,
                                     0U,
                                     "calendar invalid");
            return true;
        }
        g_self_test.rtc_phase = 1U;
        g_self_test.rtc_deadline_ms = now_ms + APP_SELF_TEST_RTC_DELAY_MS;
        return false;
    }
    if(!app_self_test_deadline_elapsed(now_ms,
                                       g_self_test.rtc_deadline_ms))
    {
        return false;
    }
    if(app_self_test_rtc_seconds(&seconds) &&
       ((int32_t)(seconds - g_self_test.rtc_reference_seconds) > 0))
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_RTC,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 seconds - g_self_test.rtc_reference_seconds,
                                 "lse counter advanced");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_RTC,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 -2,
                                 0U,
                                 "seconds did not advance");
    }
    g_self_test.rtc_phase = 0U;
    return true;
}

/** @brief Classify USB as passed only when a host has enumerated CDC. */
static void app_self_test_usb(void)
{
    if(usb_console_is_connected())
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_USB,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 1U,
                                 "cdc connected");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_USB,
                                 APP_SELF_TEST_STATUS_NOT_CONNECTED,
                                 0,
                                 0U,
                                 "host not connected");
    }
}

/** @brief Verify persistent storage initialization and transport health. */
static void app_self_test_blackbox(void)
{
    app_blackbox_snapshot_t snapshot;

    app_blackbox_get_snapshot(&snapshot);
    if(snapshot.is_initialized && (snapshot.store.io_errors == 0U))
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_BLACKBOX,
                                 APP_SELF_TEST_STATUS_PASSED,
                                 0,
                                 snapshot.store.stored_records,
                                 "journal writable");
    }
    else
    {
        app_self_test_set_result(APP_SELF_TEST_ITEM_BLACKBOX,
                                 APP_SELF_TEST_STATUS_FAILED,
                                 (int32_t)snapshot.store.io_errors,
                                 snapshot.store.corrupt_records,
                                 "journal unhealthy");
    }
}

/** @brief Execute one item and return false only for the RTC wait phase. */
static bool app_self_test_run_item(app_self_test_item_id_t item,
                                   uint32_t now_ms)
{
    switch(item)
    {
        case APP_SELF_TEST_ITEM_LED: app_self_test_led(); break;
        case APP_SELF_TEST_ITEM_LCD: app_self_test_lcd(); break;
        case APP_SELF_TEST_ITEM_BACKLIGHT_PWM: app_self_test_pwm(); break;
        case APP_SELF_TEST_ITEM_TOUCH: app_self_test_touch(); break;
        case APP_SELF_TEST_ITEM_SPI_FLASH: app_self_test_flash(); break;
        case APP_SELF_TEST_ITEM_W800: app_self_test_w800(); break;
        case APP_SELF_TEST_ITEM_UART_W800:
            app_self_test_uart(item, BSP_UART_W800_AT); break;
        case APP_SELF_TEST_ITEM_UART_DEBUG:
            app_self_test_uart(item, BSP_UART_DEBUG); break;
        case APP_SELF_TEST_ITEM_RS485_1:
            app_self_test_rs485(item, BSP_UART_RS485_1); break;
        case APP_SELF_TEST_ITEM_RS485_2:
            app_self_test_rs485(item, BSP_UART_RS485_2); break;
        case APP_SELF_TEST_ITEM_FDCAN_1:
            app_self_test_fdcan(item, true); break;
        case APP_SELF_TEST_ITEM_FDCAN_2:
            app_self_test_fdcan(item, false); break;
        case APP_SELF_TEST_ITEM_RTC:
            return app_self_test_rtc(now_ms);
        case APP_SELF_TEST_ITEM_USB: app_self_test_usb(); break;
        case APP_SELF_TEST_ITEM_BLACKBOX: app_self_test_blackbox(); break;
        default: return true;
    }
    return true;
}

/** @brief Recalculate report totals and persist one compact completion event. */
static void app_self_test_complete(uint32_t now_ms)
{
    uint32_t index;
    char summary[BLACKBOX_STORE_PAYLOAD_SIZE];
    int length;

    g_self_test.snapshot.passed_count = 0U;
    g_self_test.snapshot.failed_count = 0U;
    g_self_test.snapshot.not_connected_count = 0U;
    g_self_test.snapshot.not_installed_count = 0U;
    for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; ++index)
    {
        switch(g_self_test.snapshot.items[index].status)
        {
            case APP_SELF_TEST_STATUS_PASSED:
                ++g_self_test.snapshot.passed_count; break;
            case APP_SELF_TEST_STATUS_FAILED:
                ++g_self_test.snapshot.failed_count; break;
            case APP_SELF_TEST_STATUS_NOT_CONNECTED:
                ++g_self_test.snapshot.not_connected_count; break;
            case APP_SELF_TEST_STATUS_NOT_INSTALLED:
                ++g_self_test.snapshot.not_installed_count; break;
            default: break;
        }
    }
    g_self_test.snapshot.completed_ms = now_ms;
    g_self_test.snapshot.state = APP_SELF_TEST_STATE_COMPLETED;
    length = snprintf(summary,
                      sizeof(summary),
                      "pass=%u fail=%u offline=%u",
                      g_self_test.snapshot.passed_count,
                      g_self_test.snapshot.failed_count,
                      g_self_test.snapshot.not_connected_count);
    if(length < 0)
    {
        length = 0;
    }
    if((uint32_t)length > sizeof(summary))
    {
        length = sizeof(summary);
    }
    (void)app_blackbox_record(APP_BLACKBOX_EVENT_SELF_TEST,
                              (g_self_test.snapshot.failed_count == 0U) ?
                                  APP_BLACKBOX_SEVERITY_INFO :
                                  APP_BLACKBOX_SEVERITY_ERROR,
                              APP_SELF_TEST_SOURCE_ID,
                              g_self_test.snapshot.failed_count,
                              summary,
                              (uint16_t)length);
}

/** @brief Reset item state and begin one fresh generation. */
static void app_self_test_begin(uint32_t now_ms)
{
    uint32_t index;

    ++g_self_test.snapshot.generation;
    g_self_test.snapshot.state = APP_SELF_TEST_STATE_RUNNING;
    g_self_test.snapshot.started_ms = now_ms;
    g_self_test.snapshot.completed_ms = 0U;
    g_self_test.current_item = 0U;
    g_self_test.rtc_phase = 0U;
    for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; ++index)
    {
        app_self_test_item_t *item = &g_self_test.snapshot.items[index];

        (void)memset(item, 0, sizeof(*item));
        item->id = (app_self_test_item_id_t)index;
        item->status = APP_SELF_TEST_STATUS_NOT_RUN;
    }
}

/** @brief Initialize the self-test model and schedule one automatic run. */
void app_self_test_init(uint32_t now_ms)
{
    (void)memset(&g_self_test, 0, sizeof(g_self_test));
    g_self_test.snapshot.state = APP_SELF_TEST_STATE_WAITING;
    g_self_test.snapshot.is_initialized = 1U;
    g_self_test.start_deadline_ms = now_ms + APP_SELF_TEST_START_DELAY_MS;
}

/** @brief Advance at most one bounded self-test item. */
void app_self_test_step(uint32_t now_ms)
{
    if(g_self_test.snapshot.is_initialized == 0U)
    {
        return;
    }
    if(g_self_test.run_requested != 0U)
    {
        g_self_test.run_requested = 0U;
        app_self_test_begin(now_ms);
    }
    if((g_self_test.snapshot.state == APP_SELF_TEST_STATE_WAITING) &&
       app_self_test_deadline_elapsed(now_ms,
                                      g_self_test.start_deadline_ms))
    {
        app_self_test_begin(now_ms);
    }
    if(g_self_test.snapshot.state != APP_SELF_TEST_STATE_RUNNING)
    {
        return;
    }

    g_self_test.snapshot.items[g_self_test.current_item].status =
        APP_SELF_TEST_STATUS_TESTING;
    if(!app_self_test_run_item(
           (app_self_test_item_id_t)g_self_test.current_item,
           now_ms))
    {
        return;
    }
    ++g_self_test.current_item;
    if(g_self_test.current_item >= APP_SELF_TEST_ITEM_COUNT)
    {
        app_self_test_complete(now_ms);
    }
}

/** @brief Request a fresh run without blocking the caller. */
void app_self_test_request_run(void)
{
    g_self_test.run_requested = 1U;
}

/** @brief Copy one coherent structured report snapshot. */
void app_self_test_get_snapshot(app_self_test_snapshot_t *snapshot)
{
    bsp_irq_state_t irq_state;

    if(snapshot == NULL)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    *snapshot = g_self_test.snapshot;
    bsp_irq_unlock(irq_state);
}

/** @brief Return a stable lower_snake_case item name. */
const char *app_self_test_item_name(app_self_test_item_id_t id)
{
    return ((uint32_t)id < APP_SELF_TEST_ITEM_COUNT) ?
           g_self_test_item_names[id] : "unknown";
}

/** @brief Return a stable lower_snake_case status name. */
const char *app_self_test_status_name(app_self_test_status_t status)
{
    return ((uint32_t)status <
            (sizeof(g_self_test_status_names) /
             sizeof(g_self_test_status_names[0]))) ?
           g_self_test_status_names[status] : "unknown";
}
