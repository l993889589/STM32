/**
 * @file app_self_test.h
 * @brief Bounded whole-board automatic self-test and structured result API.
 */

#ifndef APP_SELF_TEST_H
#define APP_SELF_TEST_H

#include <stdbool.h>
#include <stdint.h>

#define APP_SELF_TEST_DETAIL_SIZE 32U

typedef enum
{
    APP_SELF_TEST_ITEM_LED = 0,
    APP_SELF_TEST_ITEM_LCD,
    APP_SELF_TEST_ITEM_BACKLIGHT_PWM,
    APP_SELF_TEST_ITEM_TOUCH,
    APP_SELF_TEST_ITEM_SPI_FLASH,
    APP_SELF_TEST_ITEM_W800,
    APP_SELF_TEST_ITEM_UART_W800,
    APP_SELF_TEST_ITEM_UART_DEBUG,
    APP_SELF_TEST_ITEM_RS485_1,
    APP_SELF_TEST_ITEM_RS485_2,
    APP_SELF_TEST_ITEM_FDCAN_1,
    APP_SELF_TEST_ITEM_FDCAN_2,
    APP_SELF_TEST_ITEM_RTC,
    APP_SELF_TEST_ITEM_USB,
    APP_SELF_TEST_ITEM_BLACKBOX,
    APP_SELF_TEST_ITEM_COUNT
} app_self_test_item_id_t;

typedef enum
{
    APP_SELF_TEST_STATUS_NOT_RUN = 0,
    APP_SELF_TEST_STATUS_TESTING,
    APP_SELF_TEST_STATUS_NOT_INSTALLED,
    APP_SELF_TEST_STATUS_NOT_CONNECTED,
    APP_SELF_TEST_STATUS_FAILED,
    APP_SELF_TEST_STATUS_PASSED
} app_self_test_status_t;

typedef enum
{
    APP_SELF_TEST_STATE_IDLE = 0,
    APP_SELF_TEST_STATE_WAITING,
    APP_SELF_TEST_STATE_RUNNING,
    APP_SELF_TEST_STATE_COMPLETED
} app_self_test_state_t;

typedef struct
{
    app_self_test_item_id_t id;
    app_self_test_status_t status;
    int32_t error_code;
    uint32_t value;
    char detail[APP_SELF_TEST_DETAIL_SIZE];
} app_self_test_item_t;

typedef struct
{
    app_self_test_item_t items[APP_SELF_TEST_ITEM_COUNT];
    app_self_test_state_t state;
    uint32_t generation;
    uint32_t started_ms;
    uint32_t completed_ms;
    uint16_t passed_count;
    uint16_t failed_count;
    uint16_t not_connected_count;
    uint16_t not_installed_count;
    uint8_t is_initialized;
} app_self_test_snapshot_t;

/** @brief Initialize the self-test model and schedule one automatic run. */
void app_self_test_init(uint32_t now_ms);

/** @brief Advance at most one bounded self-test item. */
void app_self_test_step(uint32_t now_ms);

/** @brief Request a fresh run without blocking the caller. */
void app_self_test_request_run(void);

/** @brief Copy one coherent structured report snapshot. */
void app_self_test_get_snapshot(app_self_test_snapshot_t *snapshot);

/** @brief Return a stable lower_snake_case item name. */
const char *app_self_test_item_name(app_self_test_item_id_t id);

/** @brief Return a stable lower_snake_case status name. */
const char *app_self_test_status_name(app_self_test_status_t status);

#endif /* APP_SELF_TEST_H */
