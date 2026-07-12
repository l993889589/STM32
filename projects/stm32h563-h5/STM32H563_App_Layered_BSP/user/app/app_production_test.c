/**
 * @file app_production_test.c
 * @brief Production-test orchestration, UID binding, SHA-256, and persistence.
 */

#include "app_production_test.h"

#include <string.h>

#include "app_blackbox.h"
#include "app_config.h"
#include "app_power.h"
#include "app_ui.h"
#include "bsp_dwt.h"
#include "bsp_irq_lock.h"
#include "gd25lq128.h"
#include "ota_layout.h"
#include "ota_sha256.h"

#define APP_PRODUCTION_SOURCE_ID        5U
#define APP_PRODUCTION_EVENT_COMPLETED  1U
#define APP_PRODUCTION_ID_MAGIC         0x4449444CUL
#define APP_PRODUCTION_ID_VERSION       1U
#define APP_PRODUCTION_ID_VALID         0xA55A5AA5UL
#define APP_PRODUCTION_ID_SOURCE_FLASH  1U
#define APP_PRODUCTION_ID_SOURCE_BOOT   2U

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t device_id[3];
    uint32_t check;
    uint32_t valid;
} app_production_identity_t;

typedef struct
{
    app_production_report_t report;
    uint32_t expected_self_test_generation;
    bool power_lock_held;
} app_production_context_t;

typedef struct
{
    uint32_t session_id;
    uint16_t passed_count;
    uint16_t failed_count;
    uint32_t digest_prefix;
} app_production_blackbox_payload_t;

static app_production_context_t g_production;

/** @brief Calculate the lightweight integrity word for one identity record. */
static uint32_t app_production_identity_check(
    const app_production_identity_t *identity)
{
    return identity->magic ^ identity->version ^
           identity->device_id[0] ^ identity->device_id[1] ^
           identity->device_id[2] ^ 0x6C647569UL;
}

/** @brief Load or create one persistent external-flash device identifier. */
static uint8_t app_production_get_device_id(uint32_t now_ms,
                                            uint32_t device_id[3])
{
    app_production_identity_t identity;
    app_blackbox_snapshot_t blackbox;
    uint32_t entropy[8];
    uint8_t digest[OTA_SHA256_DIGEST_SIZE];

    (void)memset(&identity, 0, sizeof(identity));
    if(gd25lq128_read(OTA_EXT_FACTORY_IDENTITY_ADDR,
                      (uint8_t *)&identity,
                      sizeof(identity)) &&
       (identity.magic == APP_PRODUCTION_ID_MAGIC) &&
       (identity.version == APP_PRODUCTION_ID_VERSION) &&
       (identity.valid == APP_PRODUCTION_ID_VALID) &&
       (identity.check == app_production_identity_check(&identity)))
    {
        (void)memcpy(device_id, identity.device_id, sizeof(identity.device_id));
        return APP_PRODUCTION_ID_SOURCE_FLASH;
    }

    app_blackbox_get_snapshot(&blackbox);
    entropy[0] = now_ms;
    entropy[1] = bsp_dwt_get_cycle();
    entropy[2] = blackbox.store.newest_sequence;
    entropy[3] = blackbox.store.program_operations;
    entropy[4] = blackbox.reset_causes;
    entropy[5] = blackbox.store.active_sector;
    entropy[6] = blackbox.store.active_slot;
    entropy[7] = blackbox.written_events ^ 0x56348502UL;
    ota_sha256_calculate((const uint8_t *)entropy,
                         sizeof(entropy),
                         digest);
    identity.magic = APP_PRODUCTION_ID_MAGIC;
    identity.version = APP_PRODUCTION_ID_VERSION;
    (void)memcpy(identity.device_id, digest, sizeof(identity.device_id));
    identity.check = app_production_identity_check(&identity);
    identity.valid = APP_PRODUCTION_ID_VALID;
    (void)memcpy(device_id, identity.device_id, sizeof(identity.device_id));
    if(gd25lq128_erase_4k(OTA_EXT_FACTORY_IDENTITY_ADDR) &&
       gd25lq128_write(OTA_EXT_FACTORY_IDENTITY_ADDR,
                       (const uint8_t *)&identity,
                       sizeof(identity)) &&
       gd25lq128_read_verify(OTA_EXT_FACTORY_IDENTITY_ADDR,
                             (const uint8_t *)&identity,
                             sizeof(identity)))
    {
        return APP_PRODUCTION_ID_SOURCE_FLASH;
    }
    return APP_PRODUCTION_ID_SOURCE_BOOT;
}

/** @brief Hash one completed report using an explicit canonical field order. */
static void app_production_test_calculate_digest(
    app_production_report_t *report)
{
    ota_sha256_context_t sha256;
    uint32_t index;

    ota_sha256_init(&sha256);
    ota_sha256_update(&sha256,
                      (const uint8_t *)&report->schema_version,
                      sizeof(report->schema_version));
    ota_sha256_update(&sha256,
                      (const uint8_t *)&report->session_id,
                      sizeof(report->session_id));
    ota_sha256_update(&sha256,
                      (const uint8_t *)report->device_id,
                      sizeof(report->device_id));
    ota_sha256_update(&sha256,
                      (const uint8_t *)APP_FIRMWARE_VERSION,
                      sizeof(APP_FIRMWARE_VERSION) - 1U);
    ota_sha256_update(&sha256,
                      (const uint8_t *)&report->self_test.generation,
                      sizeof(report->self_test.generation));
    for(index = 0U; index < APP_SELF_TEST_ITEM_COUNT; ++index)
    {
        const app_self_test_item_t *item = &report->self_test.items[index];

        ota_sha256_update(&sha256,
                          (const uint8_t *)&item->id,
                          sizeof(item->id));
        ota_sha256_update(&sha256,
                          (const uint8_t *)&item->status,
                          sizeof(item->status));
        ota_sha256_update(&sha256,
                          (const uint8_t *)&item->error_code,
                          sizeof(item->error_code));
        ota_sha256_update(&sha256,
                          (const uint8_t *)&item->value,
                          sizeof(item->value));
    }
    ota_sha256_finish(&sha256, report->digest);
    report->digest_valid = 1U;
}

/** @brief Persist one compact completed-session audit record. */
static void app_production_test_record_completion(
    const app_production_report_t *report)
{
    app_production_blackbox_payload_t payload =
    {
        .session_id = report->session_id,
        .passed_count = report->self_test.passed_count,
        .failed_count = report->self_test.failed_count,
        .digest_prefix = ((uint32_t)report->digest[0]) |
                         ((uint32_t)report->digest[1] << 8) |
                         ((uint32_t)report->digest[2] << 16) |
                         ((uint32_t)report->digest[3] << 24)
    };

    (void)app_blackbox_record(
        APP_BLACKBOX_EVENT_SELF_TEST,
        report->state == APP_PRODUCTION_STATE_PASSED ?
            APP_BLACKBOX_SEVERITY_INFO : APP_BLACKBOX_SEVERITY_ERROR,
        APP_PRODUCTION_SOURCE_ID,
        APP_PRODUCTION_EVENT_COMPLETED,
        &payload,
        (uint16_t)sizeof(payload));
}

/** @brief Start one new self-test generation and show its dedicated page. */
bool app_production_test_start(uint32_t now_ms)
{
    app_self_test_snapshot_t self_test;
    bsp_irq_state_t irq_state;
    uint32_t next_session_id;

    app_self_test_get_snapshot(&self_test);
    irq_state = bsp_irq_lock();
    if(g_production.report.state == APP_PRODUCTION_STATE_RUNNING)
    {
        bsp_irq_unlock(irq_state);
        return false;
    }
    next_session_id = g_production.report.session_id + 1U;
    (void)memset(&g_production.report, 0, sizeof(g_production.report));
    g_production.report.schema_version = APP_PRODUCTION_REPORT_SCHEMA_VERSION;
    g_production.report.session_id = next_session_id;
    if(g_production.report.session_id == 0U)
    {
        g_production.report.session_id = 1U;
    }
    g_production.report.started_ms = now_ms;
    g_production.report.state = APP_PRODUCTION_STATE_RUNNING;
    g_production.expected_self_test_generation = self_test.generation + 1U;
    g_production.power_lock_held = true;
    bsp_irq_unlock(irq_state);

    app_power_wake_lock_acquire(APP_POWER_OWNER_USER);
    g_production.report.device_id_source =
        app_production_get_device_id(now_ms,
                                     g_production.report.device_id);
    app_self_test_request_run();
    (void)app_ui_request_page(APP_UI_PAGE_BOARD_SELF_TEST);
    return true;
}

/** @brief Finalize a production report when its requested self-test completes. */
void app_production_test_step(uint32_t now_ms)
{
    app_self_test_snapshot_t self_test;

    if(g_production.report.state != APP_PRODUCTION_STATE_RUNNING)
    {
        return;
    }
    app_self_test_get_snapshot(&self_test);
    if((self_test.state != APP_SELF_TEST_STATE_COMPLETED) ||
       ((int32_t)(self_test.generation -
                  g_production.expected_self_test_generation) < 0))
    {
        return;
    }
    g_production.report.self_test = self_test;
    g_production.report.completed_ms = now_ms;
    g_production.report.state =
        (self_test.failed_count == 0U) &&
        (self_test.not_connected_count == 0U) &&
        (self_test.not_installed_count == 0U) ?
            APP_PRODUCTION_STATE_PASSED : APP_PRODUCTION_STATE_FAILED;
    app_production_test_calculate_digest(&g_production.report);
    app_production_test_record_completion(&g_production.report);
    if(g_production.power_lock_held)
    {
        app_power_wake_lock_release(APP_POWER_OWNER_USER);
        g_production.power_lock_held = false;
    }
}

/** @brief Copy a production report under a short interrupt lock. */
void app_production_test_get_report(app_production_report_t *report)
{
    bsp_irq_state_t irq_state;

    if(report == NULL)
    {
        return;
    }
    irq_state = bsp_irq_lock();
    *report = g_production.report;
    bsp_irq_unlock(irq_state);
}

/** @brief Convert a valid SHA-256 digest to uppercase hexadecimal text. */
void app_production_test_format_digest(const app_production_report_t *report,
                                       char *text,
                                       uint32_t text_size)
{
    static const char digits[] = "0123456789ABCDEF";
    uint32_t index;

    if((report == NULL) || (text == NULL) ||
       (text_size < (APP_PRODUCTION_DIGEST_SIZE * 2U + 1U)))
    {
        return;
    }
    for(index = 0U; index < APP_PRODUCTION_DIGEST_SIZE; ++index)
    {
        text[index * 2U] = digits[report->digest[index] >> 4];
        text[index * 2U + 1U] = digits[report->digest[index] & 0x0FU];
    }
    text[APP_PRODUCTION_DIGEST_SIZE * 2U] = '\0';
}

/** @brief Convert one production-test state to stable diagnostic text. */
const char *app_production_test_state_name(app_production_state_t state)
{
    switch(state)
    {
        case APP_PRODUCTION_STATE_IDLE:    return "idle";
        case APP_PRODUCTION_STATE_RUNNING: return "running";
        case APP_PRODUCTION_STATE_PASSED:  return "passed";
        case APP_PRODUCTION_STATE_FAILED:  return "failed";
        default:                           return "unknown";
    }
}
