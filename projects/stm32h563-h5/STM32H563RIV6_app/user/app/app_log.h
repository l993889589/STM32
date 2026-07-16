/**
 * @file app_log.h
 * @brief Application log record format, severity, and producer API.
 */

#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>
#include <stdint.h>

#define APP_LOG_RECORD_COUNT        64U
#define APP_LOG_MESSAGE_SIZE        96U

typedef enum
{
    APP_LOG_LEVEL_ERROR = 0,
    APP_LOG_LEVEL_WARN,
    APP_LOG_LEVEL_INFO,
    APP_LOG_LEVEL_DEBUG,
    APP_LOG_LEVEL_TRACE,
    APP_LOG_LEVEL_OFF
} app_log_level_t;

typedef enum
{
    APP_LOG_MODULE_SYS = 0,
    APP_LOG_MODULE_BSP,
    APP_LOG_MODULE_UART,
    APP_LOG_MODULE_LDC,
    APP_LOG_MODULE_SHELL,
    APP_LOG_MODULE_W800,
    APP_LOG_MODULE_MQTT,
    APP_LOG_MODULE_RS485,
    APP_LOG_MODULE_USB,
    APP_LOG_MODULE_OTA,
    APP_LOG_MODULE_UI,
    APP_LOG_MODULE_COUNT
} app_log_module_t;

typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_ms;
    app_log_module_t module;
    app_log_level_t level;
    char message[APP_LOG_MESSAGE_SIZE];
} app_log_record_t;

typedef struct
{
    uint32_t written;
    uint32_t dropped;
    uint16_t stored;
    uint8_t console_enabled;
} app_log_stats_t;

typedef int (*app_log_sink_fn)(const uint8_t *data, uint16_t length, void *arg);

void app_log_init(void);
void app_log_clear(void);
void app_log_write(app_log_module_t module, app_log_level_t level, const char *format, ...);
uint16_t app_log_read_tail(app_log_record_t *records, uint16_t max_records);
void app_log_get_stats(app_log_stats_t *stats);

bool app_log_set_level(app_log_module_t module, app_log_level_t level);
app_log_level_t app_log_get_level(app_log_module_t module);
bool app_log_parse_module(const char *text, app_log_module_t *module);
bool app_log_parse_level(const char *text, app_log_level_t *level);
const char *app_log_module_name(app_log_module_t module);
const char *app_log_level_name(app_log_level_t level);

void app_log_set_sink(app_log_sink_fn sink, void *arg);
void app_log_set_console_enabled(bool enabled);
bool app_log_console_enabled(void);

#define APP_LOGE(module, fmt, ...) app_log_write((module), APP_LOG_LEVEL_ERROR, (fmt), ##__VA_ARGS__)
#define APP_LOGW(module, fmt, ...) app_log_write((module), APP_LOG_LEVEL_WARN,  (fmt), ##__VA_ARGS__)
#define APP_LOGI(module, fmt, ...) app_log_write((module), APP_LOG_LEVEL_INFO,  (fmt), ##__VA_ARGS__)
#define APP_LOGD(module, fmt, ...) app_log_write((module), APP_LOG_LEVEL_DEBUG, (fmt), ##__VA_ARGS__)
#define APP_LOGT(module, fmt, ...) app_log_write((module), APP_LOG_LEVEL_TRACE, (fmt), ##__VA_ARGS__)

#endif /* APP_LOG_H */
