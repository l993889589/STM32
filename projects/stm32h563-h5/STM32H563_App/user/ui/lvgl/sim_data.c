#include "sim_data.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static sim_data_t s_data;
static bool s_initialized = false;

static int rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (rand() % (hi - lo + 1));
}

void sim_data_init(void) {
    memset(&s_data, 0, sizeof(s_data));

    /* System status from reference image */
    s_data.cpu_usage = 32;
    s_data.memory_usage = 48;
    s_data.storage_usage = 62;
    s_data.temperature = 38.5f;

    /* Communication */
    s_data.nearlink.connected = true;
    s_data.nearlink.rssi = -45;
    strcpy(s_data.nearlink.status_text, "已连接");

    s_data.w800_at.connected = true;
    s_data.w800_at.rssi = -52;
    strcpy(s_data.w800_at.status_text, "已连接");

    s_data.modbus.connected = true;
    s_data.modbus.stations = 3;
    strcpy(s_data.modbus.status_text, "运行中");

    s_data.mqtt.connected = true;
    s_data.mqtt.qos = 1;
    strcpy(s_data.mqtt.status_text, "已连接");

    /* Message bus */
    s_data.msg_per_sec = 126;
    s_data.msg_published = 15231;
    s_data.msg_processed = 15105;
    s_data.msg_dropped = 126;
    s_data.msg_subscribers = 8;

    /* Chart history - start with current values */
    s_data.chart_count = 0;
    for (int i = 0; i < SIM_CHART_POINTS; i++) {
        s_data.chart_cpu[i] = s_data.cpu_usage;
        s_data.chart_mem[i] = s_data.memory_usage;
        s_data.chart_net[i] = 20;
    }
    s_data.chart_count = SIM_CHART_POINTS;

    /* Alerts from reference image */
    s_data.alert_count = 2;
    s_data.alerts[0].level = SIM_ALERT_ERROR;
    strcpy(s_data.alerts[0].title, "W800连接超时");
    strcpy(s_data.alerts[0].message, "AT通信模块响应超时");
    strcpy(s_data.alerts[0].timestamp, "12:30:12");
    s_data.alerts[1].level = SIM_ALERT_WARNING;
    strcpy(s_data.alerts[1].title, "温度偏高");
    strcpy(s_data.alerts[1].message, "系统温度超过阈值: 38.5°C");
    strcpy(s_data.alerts[1].timestamp, "12:29:48");

    /* System info */
    strcpy(s_data.device_model, "LeduO W800");
    strcpy(s_data.firmware_version, "v1.3.0");
    s_data.uptime_seconds = 2 * 86400 + 14 * 3600 + 32 * 60 + 18;

    /* Start time - use a fixed reference time matching the image */
    s_data.year = 2025;
    s_data.month = 5;
    s_data.day = 26;
    s_data.hour = 12;
    s_data.minute = 28;
    s_data.second = 45;

    s_data.system_normal = true;
    s_initialized = true;
    srand(42);
}

void sim_data_tick(void) {
    if (!s_initialized) return;

    /* Advance clock */
    s_data.second++;
    if (s_data.second >= 60) {
        s_data.second = 0;
        s_data.minute++;
        if (s_data.minute >= 60) {
            s_data.minute = 0;
            s_data.hour = (s_data.hour + 1) % 24;
        }
    }

    /* Uptime */
    s_data.uptime_seconds++;

    /* CPU fluctuation 28-38% */
    s_data.cpu_usage = (uint8_t)rand_range(28, 38);

    /* Memory fluctuation 45-52% */
    s_data.memory_usage = (uint8_t)rand_range(45, 52);

    /* Temperature fluctuation 37.0-40.0 */
    s_data.temperature = 37.0f + (float)(rand() % 31) / 10.0f;

    /* Message bus - grow counters */
    s_data.msg_published += s_data.msg_per_sec;
    s_data.msg_processed += s_data.msg_per_sec - (rand() % 3);
    s_data.msg_dropped += (rand() % 3);
    s_data.msg_per_sec = (uint32_t)rand_range(110, 140);

    /* Push chart data (shift left, add new) */
    for (int i = 0; i < SIM_CHART_POINTS - 1; i++) {
        s_data.chart_cpu[i] = s_data.chart_cpu[i + 1];
        s_data.chart_mem[i] = s_data.chart_mem[i + 1];
        s_data.chart_net[i] = s_data.chart_net[i + 1];
    }
    s_data.chart_cpu[SIM_CHART_POINTS - 1] = s_data.cpu_usage;
    s_data.chart_mem[SIM_CHART_POINTS - 1] = s_data.memory_usage;
    s_data.chart_net[SIM_CHART_POINTS - 1] = (uint8_t)rand_range(15, 35);
}

const sim_data_t *sim_data_get(void) {
    return &s_data;
}
