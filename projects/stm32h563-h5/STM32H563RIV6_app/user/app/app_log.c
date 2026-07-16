/**
 * @file app_log.c
 * @brief Thread-safe bounded application log ring and output routing.
 */

#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ldc_port_irq.h"
#include "tx_api.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND 1000U
#endif

static app_log_record_t g_records[APP_LOG_RECORD_COUNT];
static app_log_level_t g_levels[APP_LOG_MODULE_COUNT];
static uint32_t g_sequence;
static uint32_t g_written;
static uint32_t g_dropped;
static uint16_t g_head;
static uint16_t g_count;
static uint8_t g_initialized;
static uint8_t g_console_enabled;
static volatile uint8_t g_sink_busy;
static app_log_sink_fn g_sink;
static void *g_sink_arg;

static const char *const g_module_names[APP_LOG_MODULE_COUNT] =
{
    "sys",
    "bsp",
    "uart",
    "ldc",
    "shell",
    "w800",
    "mqtt",
    "rs485",
    "usb",
    "ota",
    "ui"
};

static const char *const g_level_names[] =
{
    "error",
    "warn",
    "info",
    "debug",
    "trace",
    "off"
};

static uint32_t app_log_now_ms(void)
{
    return (uint32_t)(((uint64_t)tx_time_get() * 1000ULL) / (uint64_t)TX_TIMER_TICKS_PER_SECOND);
}

static void app_log_format_console_line(const app_log_record_t *record,
                                        char *line,
                                        uint16_t line_size)
{
    if(record == NULL || line == NULL || line_size == 0U)
        return;

    (void)snprintf(line,
                   line_size,
                   "[%lu] %-5s %-5s %s\r\n",
                   (unsigned long)record->timestamp_ms,
                   app_log_level_name(record->level),
                   app_log_module_name(record->module),
                   record->message);
}

void app_log_init(void)
{
    uint32_t state;

    state = ldc_port_irq_lock(NULL);
    if(g_initialized == 0U)
    {
        for(uint32_t i = 0U; i < APP_LOG_MODULE_COUNT; i++)
            g_levels[i] = APP_LOG_LEVEL_INFO;
        g_console_enabled = 0U;
        g_initialized = 1U;
    }
    ldc_port_irq_unlock(NULL, state);
}

void app_log_clear(void)
{
    uint32_t state = ldc_port_irq_lock(NULL);

    memset(g_records, 0, sizeof(g_records));
    g_head = 0U;
    g_count = 0U;
    g_dropped = 0U;
    ldc_port_irq_unlock(NULL, state);
}

void app_log_write(app_log_module_t module, app_log_level_t level, const char *format, ...)
{
    app_log_record_t record;
    uint32_t state;
    uint16_t index;
    va_list args;

    if(module >= APP_LOG_MODULE_COUNT || level >= APP_LOG_LEVEL_OFF || format == NULL)
        return;

    app_log_init();
    if(level > g_levels[module])
        return;

    memset(&record, 0, sizeof(record));
    record.timestamp_ms = app_log_now_ms();
    record.module = module;
    record.level = level;

    va_start(args, format);
    (void)vsnprintf(record.message, sizeof(record.message), format, args);
    va_end(args);
    record.message[sizeof(record.message) - 1U] = '\0';

    state = ldc_port_irq_lock(NULL);
    record.sequence = ++g_sequence;
    index = g_head;
    g_records[index] = record;
    g_head = (uint16_t)((g_head + 1U) % APP_LOG_RECORD_COUNT);
    if(g_count < APP_LOG_RECORD_COUNT)
        g_count++;
    else
        g_dropped++;
    g_written++;
    ldc_port_irq_unlock(NULL, state);

    if(g_console_enabled != 0U && g_sink != NULL && g_sink_busy == 0U)
    {
        char line[APP_LOG_MESSAGE_SIZE + 40U];
        int length;

        g_sink_busy = 1U;
        app_log_format_console_line(&record, line, (uint16_t)sizeof(line));
        length = (int)strlen(line);
        if(length > 0 && length <= UINT16_MAX)
            (void)g_sink((const uint8_t *)line, (uint16_t)length, g_sink_arg);
        g_sink_busy = 0U;
    }
}

uint16_t app_log_read_tail(app_log_record_t *records, uint16_t max_records)
{
    uint32_t state;
    uint16_t count;
    uint16_t start;

    if(records == NULL || max_records == 0U)
        return 0U;

    app_log_init();
    state = ldc_port_irq_lock(NULL);
    count = g_count;
    if(count > max_records)
        count = max_records;

    start = (uint16_t)((g_head + APP_LOG_RECORD_COUNT - count) % APP_LOG_RECORD_COUNT);
    for(uint16_t i = 0U; i < count; i++)
        records[i] = g_records[(uint16_t)((start + i) % APP_LOG_RECORD_COUNT)];

    ldc_port_irq_unlock(NULL, state);
    return count;
}

void app_log_get_stats(app_log_stats_t *stats)
{
    uint32_t state;

    if(stats == NULL)
        return;

    app_log_init();
    state = ldc_port_irq_lock(NULL);
    stats->written = g_written;
    stats->dropped = g_dropped;
    stats->stored = g_count;
    stats->console_enabled = g_console_enabled;
    ldc_port_irq_unlock(NULL, state);
}

bool app_log_set_level(app_log_module_t module, app_log_level_t level)
{
    uint32_t state;

    if(module >= APP_LOG_MODULE_COUNT || level > APP_LOG_LEVEL_OFF)
        return false;

    app_log_init();
    state = ldc_port_irq_lock(NULL);
    g_levels[module] = level;
    ldc_port_irq_unlock(NULL, state);
    return true;
}

app_log_level_t app_log_get_level(app_log_module_t module)
{
    if(module >= APP_LOG_MODULE_COUNT)
        return APP_LOG_LEVEL_OFF;

    app_log_init();
    return g_levels[module];
}

bool app_log_parse_module(const char *text, app_log_module_t *module)
{
    if(text == NULL || module == NULL)
        return false;

    for(uint32_t i = 0U; i < APP_LOG_MODULE_COUNT; i++)
    {
        if(strcmp(text, g_module_names[i]) == 0)
        {
            *module = (app_log_module_t)i;
            return true;
        }
    }
    return false;
}

bool app_log_parse_level(const char *text, app_log_level_t *level)
{
    if(text == NULL || level == NULL)
        return false;

    for(uint32_t i = 0U; i <= APP_LOG_LEVEL_OFF; i++)
    {
        if(strcmp(text, g_level_names[i]) == 0)
        {
            *level = (app_log_level_t)i;
            return true;
        }
    }
    return false;
}

const char *app_log_module_name(app_log_module_t module)
{
    if(module >= APP_LOG_MODULE_COUNT)
        return "unknown";
    return g_module_names[module];
}

const char *app_log_level_name(app_log_level_t level)
{
    if(level > APP_LOG_LEVEL_OFF)
        return "unknown";
    return g_level_names[level];
}

void app_log_set_sink(app_log_sink_fn sink, void *arg)
{
    uint32_t state = ldc_port_irq_lock(NULL);

    g_sink = sink;
    g_sink_arg = arg;
    ldc_port_irq_unlock(NULL, state);
}

void app_log_set_console_enabled(bool enabled)
{
    uint32_t state;

    app_log_init();
    state = ldc_port_irq_lock(NULL);
    g_console_enabled = enabled ? 1U : 0U;
    ldc_port_irq_unlock(NULL, state);
}

bool app_log_console_enabled(void)
{
    app_log_init();
    return g_console_enabled != 0U;
}
