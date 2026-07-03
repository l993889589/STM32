#include "ldc_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef struct
{
    unsigned int packet;
    unsigned int overflow;
    unsigned int drop;
    unsigned int callback_in_lock;
    unsigned int lock_depth;
} event_counts_t;

typedef struct
{
    ldc_t *ldc;
    uint64_t entry_rx_bytes;
    uint64_t max_rx_bytes_in_lock;
    unsigned int lock_entries;
    unsigned int lock_depth;
} atomic_lock_t;

typedef struct
{
    ldc_t *ldc;
    unsigned int lock_depth;
    unsigned int inject_once;
    unsigned int injecting;
} tick_inject_lock_t;

typedef struct
{
    ldc_t ldc;
    uint8_t ring[257];
    ldc_packet_t packets[16];
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

static void on_event(void *arg, ldc_event_t event)
{
    event_counts_t *counts = (event_counts_t *)arg;

    if(counts->lock_depth != 0U)
        counts->callback_in_lock++;

    if(event == LDC_EVT_PACKET)
        counts->packet++;
    else if(event == LDC_EVT_OVERFLOW)
        counts->overflow++;
    else if(event == LDC_EVT_DROP)
        counts->drop++;
}

static uint32_t test_lock(void *arg)
{
    event_counts_t *counts = (event_counts_t *)arg;
    counts->lock_depth++;
    return counts->lock_depth;
}

static void test_unlock(void *arg, uint32_t state)
{
    event_counts_t *counts = (event_counts_t *)arg;
    (void)state;
    CHECK(counts->lock_depth == 1U);
    counts->lock_depth--;
}

static uint32_t atomic_test_lock(void *arg)
{
    atomic_lock_t *lock = (atomic_lock_t *)arg;

    lock->lock_depth++;
    lock->lock_entries++;
    lock->entry_rx_bytes = lock->ldc->stats.rx_bytes;
    return lock->lock_depth;
}

static void atomic_test_unlock(void *arg, uint32_t state)
{
    atomic_lock_t *lock = (atomic_lock_t *)arg;
    uint64_t delta;

    (void)state;
    CHECK(lock->lock_depth == 1U);

    delta = lock->ldc->stats.rx_bytes - lock->entry_rx_bytes;
    if(delta > lock->max_rx_bytes_in_lock)
        lock->max_rx_bytes_in_lock = delta;

    lock->lock_depth--;
}

static uint32_t tick_inject_lock(void *arg)
{
    tick_inject_lock_t *lock = (tick_inject_lock_t *)arg;

    lock->lock_depth++;
    return lock->lock_depth;
}

static void tick_inject_unlock(void *arg, uint32_t state)
{
    tick_inject_lock_t *lock = (tick_inject_lock_t *)arg;

    (void)state;
    CHECK(lock->lock_depth == 1U);
    lock->lock_depth--;

    if(lock->inject_once != 0U && lock->injecting == 0U)
    {
        lock->inject_once = 0U;
        lock->injecting = 1U;
        ldc_tick(lock->ldc, 100U);
        lock->injecting = 0U;
    }
}

static void fixture_init(fixture_t *fixture,
                         uint32_t max_len,
                         uint32_t timeout_ms,
                         int delimiter,
                         ldc_mode_t mode)
{
    memset(fixture, 0, sizeof(*fixture));
    CHECK(ldc_init(&fixture->ldc,
                   fixture->ring,
                   (uint32_t)sizeof(fixture->ring),
                   fixture->packets,
                   (uint16_t)ARRAY_SIZE(fixture->packets)));
    ldc_set_frame_config(&fixture->ldc, max_len, timeout_ms, delimiter);
    ldc_set_mode(&fixture->ldc, mode);
    ldc_set_callback(&fixture->ldc, on_event, &fixture->events);
}

static int read_packet(ldc_t *ldc, const uint8_t *expected, size_t expected_len)
{
    uint8_t actual[300];
    int length = ldc_read_packet(ldc, actual, (uint32_t)sizeof(actual));

    CHECK(length == (int)expected_len);
    if(length != (int)expected_len)
        return 0;

    CHECK(memcmp(actual, expected, expected_len) == 0);
    return memcmp(actual, expected, expected_len) == 0;
}

static void test_byte_and_block_equivalence(void)
{
    static const uint8_t stream[] = "one\ntwo\nthree\n";
    fixture_t byte_input;
    fixture_t block_input;
    uint8_t byte_frame[32];
    uint8_t block_frame[32];

    fixture_init(&byte_input, 32U, 20U, '\n', LDC_MODE_PROTECT);
    fixture_init(&block_input, 32U, 20U, '\n', LDC_MODE_PROTECT);

    for(size_t i = 0U; i < sizeof(stream) - 1U; i++)
        CHECK(ldc_putc(&byte_input.ldc, stream[i]));
    CHECK(ldc_write(&block_input.ldc, stream, (uint32_t)(sizeof(stream) - 1U)) ==
          sizeof(stream) - 1U);

    CHECK(ldc_packet_available(&byte_input.ldc) == 3U);
    CHECK(ldc_packet_available(&block_input.ldc) == 3U);

    for(unsigned int i = 0U; i < 3U; i++)
    {
        int a = ldc_read_packet(&byte_input.ldc, byte_frame, sizeof(byte_frame));
        int b = ldc_read_packet(&block_input.ldc, block_frame, sizeof(block_frame));
        CHECK(a == b);
        CHECK(a > 0);
        if(a > 0 && b == a)
            CHECK(memcmp(byte_frame, block_frame, (size_t)a) == 0);
    }
}

static void test_invalid_initialization_is_rejected(void)
{
    ldc_t ldc;
    uint8_t ring[8];
    ldc_packet_t packets[2];

    CHECK(!ldc_init(NULL, ring, sizeof(ring), packets, 2U));
    CHECK(!ldc_init(&ldc, NULL, sizeof(ring), packets, 2U));
    CHECK(!ldc_init(&ldc, ring, 1U, packets, 2U));
    CHECK(!ldc_init(&ldc, ring, sizeof(ring), NULL, 2U));
    CHECK(!ldc_init(&ldc, ring, sizeof(ring), packets, 0U));
    CHECK(ldc_packet_available(NULL) == 0U);
    CHECK(ldc_write(NULL, ring, sizeof(ring)) == 0U);
    CHECK(!ldc_putc(NULL, 0U));
    CHECK(!ldc_flush(NULL));
    CHECK(!ldc_frame_pending(NULL));
}

static void test_delimiter_fixed_timeout_and_flush(void)
{
    fixture_t fixture;
    static const uint8_t delimited[] = {'A', 'B', '\n'};
    static const uint8_t fixed[] = {1U, 2U, 3U, 4U};
    static const uint8_t timeout[] = {9U, 8U, 7U};
    static const uint8_t flushed[] = {0xA5U, 0x5AU};

    fixture_init(&fixture, 32U, 10U, '\n', LDC_MODE_PROTECT);
    CHECK(ldc_write(&fixture.ldc, delimited, sizeof(delimited)) == sizeof(delimited));
    CHECK(fixture.events.packet == 1U);
    CHECK(read_packet(&fixture.ldc, delimited, sizeof(delimited)) != 0);

    fixture_init(&fixture, 4U, 0U, -1, LDC_MODE_PROTECT);
    CHECK(ldc_write(&fixture.ldc, fixed, sizeof(fixed)) == sizeof(fixed));
    CHECK(read_packet(&fixture.ldc, fixed, sizeof(fixed)) != 0);

    fixture_init(&fixture, 32U, 5U, -1, LDC_MODE_PROTECT);
    CHECK(ldc_write(&fixture.ldc, timeout, sizeof(timeout)) == sizeof(timeout));
    CHECK(ldc_frame_pending(&fixture.ldc));
    ldc_tick(&fixture.ldc, 4U);
    CHECK(ldc_packet_available(&fixture.ldc) == 0U);
    ldc_tick(&fixture.ldc, 1U);
    CHECK(!ldc_frame_pending(&fixture.ldc));
    CHECK(read_packet(&fixture.ldc, timeout, sizeof(timeout)) != 0);

    fixture_init(&fixture, 32U, 0U, -1, LDC_MODE_PROTECT);
    CHECK(ldc_write(&fixture.ldc, flushed, sizeof(flushed)) == sizeof(flushed));
    CHECK(ldc_flush(&fixture.ldc));
    CHECK(read_packet(&fixture.ldc, flushed, sizeof(flushed)) != 0);
}

static void test_timeout_activity_and_large_elapsed(void)
{
    fixture_t fixture;
    static const uint8_t data[] = {1U, 2U};

    fixture_init(&fixture, 32U, 5U, -1, LDC_MODE_PROTECT);
    CHECK(ldc_write(&fixture.ldc, data, sizeof(data)) == sizeof(data));
    ldc_tick(&fixture.ldc, 4U);
    ldc_rx_activity(&fixture.ldc);
    ldc_tick(&fixture.ldc, 4U);
    CHECK(ldc_packet_available(&fixture.ldc) == 0U);
    ldc_tick(&fixture.ldc, 1000U);
    CHECK(ldc_packet_available(&fixture.ldc) == 1U);
}

static void test_wrap_and_small_read_retry(void)
{
    fixture_t fixture;
    uint8_t first[200];
    uint8_t second[100];
    uint8_t too_small[16];

    for(size_t i = 0U; i < sizeof(first); i++)
        first[i] = (uint8_t)i;
    for(size_t i = 0U; i < sizeof(second); i++)
        second[i] = (uint8_t)(0xE0U + i);

    fixture_init(&fixture, 200U, 0U, -1, LDC_MODE_PROTECT);
    CHECK(ldc_write(&fixture.ldc, first, sizeof(first)) == sizeof(first));
    CHECK(read_packet(&fixture.ldc, first, sizeof(first)) != 0);

    ldc_set_frame_config(&fixture.ldc, 100U, 0U, -1);
    CHECK(ldc_write(&fixture.ldc, second, sizeof(second)) == sizeof(second));
    CHECK(ldc_read_packet(&fixture.ldc, too_small, sizeof(too_small)) == -1);
    CHECK(ldc_packet_available(&fixture.ldc) == 1U);
    CHECK(read_packet(&fixture.ldc, second, sizeof(second)) != 0);
}

static void test_protect_and_overwrite(void)
{
    uint8_t ring[32];
    ldc_packet_t packets[2];
    ldc_t ldc;
    event_counts_t events = {0};
    static const uint8_t a[] = "A\n";
    static const uint8_t b[] = "B\n";
    static const uint8_t c[] = "C\n";
    static const uint8_t d[] = "D\n";

    CHECK(ldc_init(&ldc, ring, sizeof(ring), packets, 2U));
    ldc_set_frame_config(&ldc, 8U, 0U, '\n');
    ldc_set_mode(&ldc, LDC_MODE_PROTECT);
    ldc_set_callback(&ldc, on_event, &events);
    CHECK(ldc_write(&ldc, a, sizeof(a) - 1U) == sizeof(a) - 1U);
    CHECK(ldc_write(&ldc, b, sizeof(b) - 1U) == sizeof(b) - 1U);
    CHECK(ldc_write(&ldc, c, sizeof(c) - 1U) == sizeof(c) - 1U);
    CHECK(ldc_packet_available(&ldc) == 2U);
    CHECK(events.overflow >= 1U);
    CHECK(read_packet(&ldc, a, sizeof(a) - 1U) != 0);
    CHECK(read_packet(&ldc, b, sizeof(b) - 1U) != 0);
    CHECK(ldc_write(&ldc, d, sizeof(d) - 1U) == sizeof(d) - 1U);
    CHECK(read_packet(&ldc, d, sizeof(d) - 1U) != 0);

    memset(&events, 0, sizeof(events));
    CHECK(ldc_init(&ldc, ring, sizeof(ring), packets, 2U));
    ldc_set_frame_config(&ldc, 8U, 0U, '\n');
    ldc_set_mode(&ldc, LDC_MODE_OVERWRITE);
    ldc_set_callback(&ldc, on_event, &events);
    CHECK(ldc_write(&ldc, a, sizeof(a) - 1U) == sizeof(a) - 1U);
    CHECK(ldc_write(&ldc, b, sizeof(b) - 1U) == sizeof(b) - 1U);
    CHECK(ldc_write(&ldc, c, sizeof(c) - 1U) == sizeof(c) - 1U);
    CHECK(ldc_packet_available(&ldc) == 2U);
    CHECK(events.drop == 1U);
    CHECK(read_packet(&ldc, b, sizeof(b) - 1U) != 0);
    CHECK(read_packet(&ldc, c, sizeof(c) - 1U) != 0);
}

static void test_callbacks_run_after_unlock(void)
{
    fixture_t fixture;
    static const uint8_t line[] = "ready\n";

    fixture_init(&fixture, 32U, 0U, '\n', LDC_MODE_PROTECT);
    ldc_set_lock(&fixture.ldc, test_lock, test_unlock, &fixture.events);
    CHECK(ldc_write(&fixture.ldc, line, sizeof(line) - 1U) == sizeof(line) - 1U);
    CHECK(fixture.events.packet == 1U);
    CHECK(fixture.events.callback_in_lock == 0U);
    CHECK(fixture.events.lock_depth == 0U);
}

static void test_default_atomic_write_limit(void)
{
    fixture_t fixture;
    atomic_lock_t lock;
    uint8_t data[100];

    memset(data, 0xA5, sizeof(data));
    memset(&lock, 0, sizeof(lock));

    fixture_init(&fixture, 0U, 0U, -1, LDC_MODE_PROTECT);
    lock.ldc = &fixture.ldc;
    ldc_set_lock(&fixture.ldc, atomic_test_lock, atomic_test_unlock, &lock);

    CHECK(fixture.ldc.atomic_write_bytes == LDC_DEFAULT_ATOMIC_WRITE_BYTES);
    CHECK(ldc_write(&fixture.ldc, data, sizeof(data)) == sizeof(data));
    CHECK(lock.max_rx_bytes_in_lock <= LDC_DEFAULT_ATOMIC_WRITE_BYTES);
    CHECK(lock.lock_entries >= 4U);
    CHECK(lock.lock_depth == 0U);
}

static void test_tick_does_not_split_atomic_write(void)
{
    fixture_t fixture;
    tick_inject_lock_t lock;
    uint8_t data[100];

    memset(data, 0x5A, sizeof(data));
    memset(&lock, 0, sizeof(lock));

    fixture_init(&fixture, 0U, 1U, false, 0U);
    lock.ldc = &fixture.ldc;
    lock.inject_once = 1U;
    ldc_set_lock(&fixture.ldc, tick_inject_lock, tick_inject_unlock, &lock);

    CHECK(ldc_write(&fixture.ldc, data, sizeof(data)) == sizeof(data));
    CHECK(ldc_packet_available(&fixture.ldc) == 0U);
    ldc_tick(&fixture.ldc, 1U);
    CHECK(ldc_packet_available(&fixture.ldc) == 1U);
    CHECK(read_packet(&fixture.ldc, data, sizeof(data)) != 0);
}

static uint32_t prng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void drain_packets(ldc_t *ldc, uint8_t *out, size_t *out_len, size_t out_size)
{
    while(ldc_packet_available(ldc) != 0U)
    {
        uint8_t frame[160];
        int length = ldc_read_packet(ldc, frame, sizeof(frame));

        CHECK(length > 0);
        if(length <= 0)
            return;

        CHECK(*out_len + (size_t)length <= out_size);
        if(*out_len + (size_t)length <= out_size)
        {
            memcpy(&out[*out_len], frame, (size_t)length);
            *out_len += (size_t)length;
        }
    }
}

static void test_atomic_split_matches_unsplit(void)
{
    uint8_t stream[8192];
    uint8_t split_out[8192];
    uint8_t unsplit_out[8192];
    size_t split_len = 0U;
    size_t unsplit_len = 0U;
    fixture_t split;
    fixture_t unsplit;
    uint32_t random = 0x31415926U;

    for(size_t i = 0U; i < sizeof(stream); i++)
    {
        stream[i] = (uint8_t)('a' + (prng_next(&random) % 26U));
        if((i % 29U) == 28U)
            stream[i] = '\n';
    }

    fixture_init(&split, 128U, 0U, '\n', LDC_MODE_PROTECT);
    fixture_init(&unsplit, 128U, 0U, '\n', LDC_MODE_PROTECT);
    ldc_set_atomic_write_bytes(&unsplit.ldc, 0U);

    random = 0x27182818U;
    for(size_t offset = 0U; offset < sizeof(stream);)
    {
        size_t chunk = 1U + (prng_next(&random) % 211U);
        if(chunk > sizeof(stream) - offset)
            chunk = sizeof(stream) - offset;

        CHECK(ldc_write(&split.ldc, &stream[offset], (uint32_t)chunk) == chunk);
        CHECK(ldc_write(&unsplit.ldc, &stream[offset], (uint32_t)chunk) == chunk);
        offset += chunk;

        drain_packets(&split.ldc, split_out, &split_len, sizeof(split_out));
        drain_packets(&unsplit.ldc, unsplit_out, &unsplit_len, sizeof(unsplit_out));
    }

    drain_packets(&split.ldc, split_out, &split_len, sizeof(split_out));
    drain_packets(&unsplit.ldc, unsplit_out, &unsplit_len, sizeof(unsplit_out));

    CHECK(split.events.packet == unsplit.events.packet);
    CHECK(split_len == unsplit_len);
    CHECK(memcmp(split_out, unsplit_out, split_len) == 0);
}

static void test_random_chunk_invariance(void)
{
    uint8_t stream[4096];
    uint8_t expected[4096];
    uint8_t actual[4096];
    size_t expected_len = 0U;
    size_t actual_len = 0U;
    fixture_t fixture;
    uint32_t random = 0x12345678U;

    for(size_t i = 0U; i < sizeof(stream); i++)
    {
        stream[i] = (uint8_t)('A' + (prng_next(&random) % 26U));
        if((i % 31U) == 30U)
            stream[i] = '\n';
    }

    fixture_init(&fixture, 64U, 0U, '\n', LDC_MODE_PROTECT);
    random = 0xC001D00DU;
    for(size_t offset = 0U; offset < sizeof(stream);)
    {
        size_t chunk = 1U + (prng_next(&random) % 47U);
        if(chunk > sizeof(stream) - offset)
            chunk = sizeof(stream) - offset;
        CHECK(ldc_write(&fixture.ldc, &stream[offset], (uint32_t)chunk) == chunk);
        offset += chunk;

        while(ldc_packet_available(&fixture.ldc) != 0U)
        {
            uint8_t frame[80];
            int length = ldc_read_packet(&fixture.ldc, frame, sizeof(frame));
            CHECK(length > 0);
            if(length <= 0)
                break;
            CHECK(actual_len + (size_t)length <= sizeof(actual));
            memcpy(&actual[actual_len], frame, (size_t)length);
            actual_len += (size_t)length;
        }
    }

    for(size_t i = 0U; i < sizeof(stream); i++)
    {
        expected[expected_len++] = stream[i];
        if(stream[i] == '\n')
            continue;
    }

    CHECK(actual_len == (sizeof(stream) / 31U) * 31U);
    CHECK(memcmp(actual, expected, actual_len) == 0);
}

static void benchmark_block_write_case(const char *label, uint32_t atomic_write_bytes)
{
    fixture_t fixture;
    uint8_t block[256];
    uint8_t frame[256];
    const unsigned int loops = 200000U;
    clock_t start;
    clock_t stop;
    double seconds;
    double mib_per_second;

    memset(block, 0x5AU, sizeof(block));
    fixture_init(&fixture, sizeof(block), 0U, -1, LDC_MODE_PROTECT);
    if(atomic_write_bytes == 0U)
        ldc_set_atomic_write_bytes(&fixture.ldc, 0U);
    else
        ldc_set_atomic_write_bytes(&fixture.ldc, atomic_write_bytes);

    start = clock();
    for(unsigned int i = 0U; i < loops; i++)
    {
        CHECK(ldc_write(&fixture.ldc, block, sizeof(block)) == sizeof(block));
        CHECK(ldc_read_packet(&fixture.ldc, frame, sizeof(frame)) == (int)sizeof(frame));
    }
    stop = clock();

    seconds = (double)(stop - start) / (double)CLOCKS_PER_SEC;
    mib_per_second = seconds > 0.0 ?
                     ((double)sizeof(block) * loops / (1024.0 * 1024.0)) / seconds : 0.0;
    (void)printf("LDC host %s: %.2f MiB/s (%u x %u bytes)\n",
                 label,
                 mib_per_second,
                 loops,
                 (unsigned int)sizeof(block));
}

static void benchmark_block_write(void)
{
    benchmark_block_write_case("split-32 write", LDC_DEFAULT_ATOMIC_WRITE_BYTES);
    benchmark_block_write_case("unsplit write", 0U);
}

int main(void)
{
    test_byte_and_block_equivalence();
    test_invalid_initialization_is_rejected();
    test_delimiter_fixed_timeout_and_flush();
    test_timeout_activity_and_large_elapsed();
    test_wrap_and_small_read_retry();
    test_protect_and_overwrite();
    test_callbacks_run_after_unlock();
    test_default_atomic_write_limit();
    test_tick_does_not_split_atomic_write();
    test_atomic_split_matches_unsplit();
    test_random_chunk_invariance();
    benchmark_block_write();

    if(g_failures != 0U)
    {
        (void)fprintf(stderr, "LDC tests failed: %u\n", g_failures);
        return EXIT_FAILURE;
    }

    (void)printf("LDC tests passed\n");
    return EXIT_SUCCESS;
}
