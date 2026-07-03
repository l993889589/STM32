#include "ldc_easy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef struct
{
    unsigned int packet;
    unsigned int overflow;
    unsigned int drop;
} event_counts_t;

typedef struct
{
    ldc_easy_t queue;
    uint8_t ring[65];
    ldc_packet_t packets[4];
    event_counts_t events;
} fixture_t;

static unsigned int g_failures;

#define CHECK(expr)                                                                    \
    do                                                                                 \
    {                                                                                  \
        if(!(expr))                                                                    \
        {                                                                              \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
            g_failures++;                                                              \
        }                                                                              \
    } while(0)

static void on_event(ldc_easy_t *queue, ldc_easy_event_t event, void *arg)
{
    event_counts_t *events = (event_counts_t *)arg;

    CHECK(queue != NULL);

    if(event == LDC_EASY_EVT_PACKET)
        events->packet++;
    else if(event == LDC_EASY_EVT_OVERFLOW)
        events->overflow++;
    else
        events->drop++;
}

static void fixture_init(fixture_t *fixture,
                         uint32_t max_frame,
                         uint32_t timeout_ms,
                         bool delimiter_enabled,
                         uint8_t delimiter)
{
    ldc_easy_config_t config;

    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    config.ring_buffer = fixture->ring;
    config.ring_size = (uint32_t)sizeof(fixture->ring);
    config.packet_pool = fixture->packets;
    config.packet_count = (uint16_t)ARRAY_SIZE(fixture->packets);
    config.max_frame = max_frame;
    config.timeout_ms = timeout_ms;
    config.delimiter_enabled = delimiter_enabled;
    config.delimiter = delimiter;
    config.mode = LDC_MODE_PROTECT;
    config.event_cb = on_event;
    config.event_arg = &fixture->events;

    CHECK(ldc_easy_init(&fixture->queue, &config));
}

static void expect_packet(ldc_easy_t *queue, const uint8_t *expected, size_t expected_len)
{
    uint8_t actual[80];
    int length = ldc_easy_pop(queue, actual, (uint32_t)sizeof(actual));

    CHECK(length == (int)expected_len);
    if(length == (int)expected_len)
        CHECK(memcmp(actual, expected, expected_len) == 0);
}

static void test_byte_irq_tick_model(void)
{
    fixture_t fixture;
    static const uint8_t a[] = {'A', 'T'};
    static const uint8_t b[] = {'+', 'O', 'K'};
    static const uint8_t expected[] = {'A', 'T', '+', 'O', 'K'};

    fixture_init(&fixture, 32U, 5U, false, 0U);

    CHECK(ldc_easy_add(&fixture.queue, a, sizeof(a)) == sizeof(a));
    CHECK(ldc_easy_available(&fixture.queue) == 0U);
    ldc_easy_tick(&fixture.queue, 4U);
    CHECK(ldc_easy_available(&fixture.queue) == 0U);

    CHECK(ldc_easy_add(&fixture.queue, b, sizeof(b)) == sizeof(b));
    ldc_easy_tick(&fixture.queue, 5U);

    CHECK(fixture.events.packet == 1U);
    CHECK(ldc_easy_available(&fixture.queue) == 1U);
    expect_packet(&fixture.queue, expected, sizeof(expected));
}

static void test_receive_to_idle_model(void)
{
    fixture_t fixture;
    static const uint8_t first[] = {'H', '7', '-', 'O', 'K'};
    static const uint8_t second[] = {'D', 'M', 'A'};

    fixture_init(&fixture, 32U, 0U, false, 0U);

    CHECK(ldc_easy_rx_idle(&fixture.queue, first, sizeof(first)) == sizeof(first));
    CHECK(ldc_easy_available(&fixture.queue) == 1U);
    expect_packet(&fixture.queue, first, sizeof(first));

    CHECK(ldc_easy_add(&fixture.queue, second, sizeof(second)) == sizeof(second));
    CHECK(ldc_easy_settle(&fixture.queue));
    expect_packet(&fixture.queue, second, sizeof(second));
}

static void test_delimiter_model(void)
{
    fixture_t fixture;
    static const uint8_t stream[] = {'o', 'n', 'e', '\n', 't', 'w', 'o', '\n'};
    static const uint8_t one[] = {'o', 'n', 'e', '\n'};
    static const uint8_t two[] = {'t', 'w', 'o', '\n'};

    fixture_init(&fixture, 32U, 0U, true, (uint8_t)'\n');

    CHECK(ldc_easy_add(&fixture.queue, stream, sizeof(stream)) == sizeof(stream));
    CHECK(ldc_easy_available(&fixture.queue) == 2U);
    expect_packet(&fixture.queue, one, sizeof(one));
    expect_packet(&fixture.queue, two, sizeof(two));
}

static void test_zero_initialized_config_does_not_enable_nul_delimiter(void)
{
    fixture_t fixture;
    static const uint8_t data[] = {'A', 0U, 'B'};

    fixture_init(&fixture, 32U, 3U, false, 0U);

    CHECK(ldc_easy_add(&fixture.queue, data, sizeof(data)) == sizeof(data));
    CHECK(ldc_easy_available(&fixture.queue) == 0U);
    ldc_easy_tick(&fixture.queue, 3U);
    expect_packet(&fixture.queue, data, sizeof(data));
}

static void test_small_read_retry_is_preserved(void)
{
    fixture_t fixture;
    uint8_t small[2];
    static const uint8_t data[] = {'A', 'B', 'C'};

    fixture_init(&fixture, 32U, 0U, false, 0U);

    CHECK(ldc_easy_rx_idle(&fixture.queue, data, sizeof(data)) == sizeof(data));
    CHECK(ldc_easy_pop(&fixture.queue, small, sizeof(small)) == -1);
    CHECK(ldc_easy_available(&fixture.queue) == 1U);
    expect_packet(&fixture.queue, data, sizeof(data));
}

int main(void)
{
    test_byte_irq_tick_model();
    test_receive_to_idle_model();
    test_delimiter_model();
    test_zero_initialized_config_does_not_enable_nul_delimiter();
    test_small_read_retry_is_preserved();

    if(g_failures != 0U)
    {
        (void)fprintf(stderr, "LDC easy tests failed: %u\n", g_failures);
        return EXIT_FAILURE;
    }

    (void)printf("LDC easy tests passed\n");
    return EXIT_SUCCESS;
}
