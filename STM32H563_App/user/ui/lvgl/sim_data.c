#include "sim_data.h"

#include <stdlib.h>
#include <string.h>

static sim_data_t s_data;
static bool s_initialized;

static int rand_range(int lo, int hi)
{
    if(hi <= lo)
        return lo;
    return lo + (rand() % (hi - lo + 1));
}

static float rand_temp(float base, int tenths_span)
{
    int delta = rand_range(-tenths_span, tenths_span);
    return base + ((float)delta / 10.0f);
}

void sim_data_init(void)
{
    memset(&s_data, 0, sizeof(s_data));

    s_data.cpu_usage = 42U;
    s_data.gpu_usage = 36U;
    s_data.memory_usage = 51U;
    s_data.storage_usage = 68U;
    s_data.fan_load = 58U;
    s_data.humidity = 43U;
    s_data.cpu_freq_mhz = 420U;
    s_data.board_voltage_mv = 3310U;
    s_data.cpu_temperature = 42.5f;
    s_data.gpu_temperature = 39.8f;
    s_data.rear_temperature = 36.4f;
    s_data.front_temperature = 35.8f;
    s_data.temperature = s_data.cpu_temperature;

    s_data.nearlink.connected = true;
    s_data.nearlink.rssi = -45;
    strcpy(s_data.nearlink.status_text, "已连接");

    s_data.w800_at.connected = true;
    s_data.w800_at.rssi = -52;
    strcpy(s_data.w800_at.status_text, "已连接");

    s_data.modbus.connected = true;
    s_data.modbus.stations = 3U;
    strcpy(s_data.modbus.status_text, "运行中");

    s_data.mqtt.connected = true;
    s_data.mqtt.qos = 1U;
    strcpy(s_data.mqtt.status_text, "已连接");

    s_data.msg_per_sec = 126U;
    s_data.msg_published = 15231U;
    s_data.msg_processed = 15105U;
    s_data.msg_dropped = 4U;
    s_data.msg_subscribers = 8U;

    for(uint32_t i = 0U; i < SIM_CHART_POINTS; i++)
    {
        s_data.chart_cpu[i] = s_data.cpu_usage;
        s_data.chart_mem[i] = s_data.memory_usage;
        s_data.chart_net[i] = 24U;
    }
    s_data.chart_count = SIM_CHART_POINTS;

    for(uint32_t i = 0U; i < SIM_DISK_COUNT; i++)
        s_data.disk_health[i] = (uint8_t)(92U - (i * 3U));

    s_data.alert_count = 3U;
    s_data.alerts[0].level = SIM_ALERT_INFO;
    strcpy(s_data.alerts[0].title, "系统启动");
    strcpy(s_data.alerts[0].message, "监控任务已进入运行态");
    strcpy(s_data.alerts[0].timestamp, "12:28:45");
    s_data.alerts[1].level = SIM_ALERT_WARNING;
    strcpy(s_data.alerts[1].title, "温度偏高");
    strcpy(s_data.alerts[1].message, "后侧温度接近预警阈值");
    strcpy(s_data.alerts[1].timestamp, "12:29:48");
    s_data.alerts[2].level = SIM_ALERT_INFO;
    strcpy(s_data.alerts[2].title, "硬盘巡检");
    strcpy(s_data.alerts[2].message, "SDA 至 SDF 均在线");
    strcpy(s_data.alerts[2].timestamp, "12:30:12");

    strcpy(s_data.device_model, "CYENT H5");
    strcpy(s_data.firmware_version, "v1.3.0");
    s_data.uptime_seconds = 2U * 86400U + 14U * 3600U + 32U * 60U + 18U;

    s_data.year = 2026;
    s_data.month = 7;
    s_data.day = 6;
    s_data.hour = 12;
    s_data.minute = 28;
    s_data.second = 45;
    s_data.system_normal = true;

    srand(42U);
    s_initialized = true;
}

void sim_data_tick(void)
{
    if(!s_initialized)
        return;

    s_data.second++;
    if(s_data.second >= 60)
    {
        s_data.second = 0;
        s_data.minute++;
        if(s_data.minute >= 60)
        {
            s_data.minute = 0;
            s_data.hour = (s_data.hour + 1) % 24;
        }
    }

    s_data.uptime_seconds++;
    s_data.cpu_usage = (uint8_t)rand_range(34, 58);
    s_data.gpu_usage = (uint8_t)rand_range(28, 54);
    s_data.memory_usage = (uint8_t)rand_range(46, 62);
    s_data.storage_usage = (uint8_t)rand_range(64, 72);
    s_data.fan_load = (uint8_t)rand_range(48, 76);
    s_data.humidity = (uint8_t)rand_range(38, 51);
    s_data.cpu_freq_mhz = (uint16_t)rand_range(398, 426);
    s_data.board_voltage_mv = (uint16_t)rand_range(3290, 3330);
    s_data.cpu_temperature = rand_temp(42.0f, 16);
    s_data.gpu_temperature = rand_temp(39.5f, 14);
    s_data.rear_temperature = rand_temp(36.8f, 10);
    s_data.front_temperature = rand_temp(35.6f, 9);
    s_data.temperature = s_data.cpu_temperature;

    s_data.msg_per_sec = (uint32_t)rand_range(110, 148);
    s_data.msg_published += s_data.msg_per_sec;
    s_data.msg_processed += s_data.msg_per_sec - (uint32_t)(rand() % 3);
    s_data.msg_dropped += (uint32_t)(rand() % 2);

    for(uint32_t i = 0U; i < SIM_CHART_POINTS - 1U; i++)
    {
        s_data.chart_cpu[i] = s_data.chart_cpu[i + 1U];
        s_data.chart_mem[i] = s_data.chart_mem[i + 1U];
        s_data.chart_net[i] = s_data.chart_net[i + 1U];
    }
    s_data.chart_cpu[SIM_CHART_POINTS - 1U] = s_data.cpu_usage;
    s_data.chart_mem[SIM_CHART_POINTS - 1U] = s_data.memory_usage;
    s_data.chart_net[SIM_CHART_POINTS - 1U] = (uint8_t)rand_range(18, 42);

    for(uint32_t i = 0U; i < SIM_DISK_COUNT; i++)
    {
        int health = 92 - (int)(i * 3U) + rand_range(-2, 2);
        if(health < 0)
            health = 0;
        if(health > 100)
            health = 100;
        s_data.disk_health[i] = (uint8_t)health;
    }
}

const sim_data_t *sim_data_get(void)
{
    return &s_data;
}
