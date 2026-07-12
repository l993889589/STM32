/**
 * @file app_production_test.h
 * @brief Device-bound whole-board production-test session and report API.
 */

#ifndef APP_PRODUCTION_TEST_H
#define APP_PRODUCTION_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "app_self_test.h"

#define APP_PRODUCTION_REPORT_SCHEMA_VERSION 1U
#define APP_PRODUCTION_DIGEST_SIZE           32U

typedef enum
{
    APP_PRODUCTION_STATE_IDLE = 0,
    APP_PRODUCTION_STATE_RUNNING,
    APP_PRODUCTION_STATE_PASSED,
    APP_PRODUCTION_STATE_FAILED
} app_production_state_t;

typedef struct
{
    uint32_t schema_version;
    uint32_t session_id;
    uint32_t started_ms;
    uint32_t completed_ms;
    uint32_t device_id[3];
    uint8_t device_id_source;
    app_production_state_t state;
    app_self_test_snapshot_t self_test;
    uint8_t digest[APP_PRODUCTION_DIGEST_SIZE];
    uint8_t digest_valid;
} app_production_report_t;

/** @brief Start one exclusive whole-board production-test session. */
bool app_production_test_start(uint32_t now_ms);

/** @brief Advance report finalization after the self-test state machine. */
void app_production_test_step(uint32_t now_ms);

/** @brief Copy one coherent production-test report. */
void app_production_test_get_report(app_production_report_t *report);

/** @brief Format the report SHA-256 digest as uppercase hexadecimal text. */
void app_production_test_format_digest(const app_production_report_t *report,
                                       char *text,
                                       uint32_t text_size);

/** @brief Return a stable lower_snake_case production state name. */
const char *app_production_test_state_name(app_production_state_t state);

#endif /* APP_PRODUCTION_TEST_H */
