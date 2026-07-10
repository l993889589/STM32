/* Host tests for OTA trial health observation, staleness and fault latching. */
#include "app_health.h"
#include <stdio.h>

static ULONG fake_ticks;
static int test_failures;

#define TEST_CHECK(condition) \
    do { if(!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        test_failures++; \
    } } while(0)

ULONG tx_time_get(void)
{
    return fake_ticks;
}

static void report_all_services(void)
{
    app_health_report(APP_HEALTH_SERVICE_RS485);
    app_health_report(APP_HEALTH_SERVICE_W800);
    app_health_report(APP_HEALTH_SERVICE_UI);
}

static void test_observation_and_stale_gate(void)
{
    app_health_status_t status;

    fake_ticks = 100U;
    app_health_init();
    report_all_services();
    TEST_CHECK(app_health_is_ready(60000U, 30000U, &status) == 0U);

    fake_ticks = 60100U;
    report_all_services();
    TEST_CHECK(app_health_is_ready(60000U, 30000U, &status) != 0U);

    fake_ticks += 30001U;
    app_health_report(APP_HEALTH_SERVICE_RS485);
    app_health_report(APP_HEALTH_SERVICE_UI);
    TEST_CHECK(app_health_is_ready(60000U, 30000U, &status) == 0U);
    TEST_CHECK((status.stale_mask & (1UL << APP_HEALTH_SERVICE_W800)) != 0U);
}

static void test_missing_service_and_fatal_fault(void)
{
    app_health_status_t status;

    fake_ticks = 0U;
    app_health_init();
    app_health_report(APP_HEALTH_SERVICE_RS485);
    app_health_report(APP_HEALTH_SERVICE_UI);
    fake_ticks = 60000U;
    TEST_CHECK(app_health_is_ready(60000U, 30000U, &status) == 0U);

    app_health_report(APP_HEALTH_SERVICE_W800);
    app_health_report_fault(APP_HEALTH_FAULT_THREAD_STACK);
    TEST_CHECK(app_health_is_ready(60000U, 30000U, &status) == 0U);
    TEST_CHECK(status.fatal_fault == APP_HEALTH_FAULT_THREAD_STACK);
}

static void test_tick_wrap(void)
{
    app_health_status_t status;

    fake_ticks = (ULONG)~0UL - 100U;
    app_health_init();
    report_all_services();
    fake_ticks += 200U;
    report_all_services();
    TEST_CHECK(app_health_is_ready(100U, 50U, &status) != 0U);
}

int main(void)
{
    test_observation_and_stale_gate();
    test_missing_service_and_fatal_fault();
    test_tick_wrap();

    if(test_failures != 0)
    {
        printf("app_health: %d failure(s)\n", test_failures);
        return 1;
    }

    printf("app_health: all tests passed\n");
    return 0;
}
