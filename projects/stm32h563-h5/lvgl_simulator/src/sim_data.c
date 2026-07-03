#include "sim_data.h"
#include <stdlib.h>
#include <string.h>

#define TXT_ONLINE      "\xE5\x9C\xA8\xE7\xBA\xBF"
#define TXT_READY       "\xE5\xB0\xB1\xE7\xBB\xAA"
#define TXT_RUNNING     "\xE8\xBF\x90\xE8\xA1\x8C"
#define TXT_SYNC        "\xE5\x90\x8C\xE6\xAD\xA5"
#define TXT_UART_ECHO   "\xE4\xB8\xB2\xE5\x8F\xA3\xE5\x9B\x9E\xE6\x98\xBE"
#define TXT_UART_MSG    "\xE4\xB8\xB2\xE5\x8F\xA3\xE4\xB8\x89\xE5\xB7\xB2\xE7\xBB\x8F\xE8\xBF\x9B\xE5\x85\xA5\xE5\x9B\x9E\xE6\x98\xBE\xE6\xA8\xA1\xE5\xBC\x8F"
#define TXT_CMD_CH      "\xE5\x91\xBD\xE4\xBB\xA4\xE9\x80\x9A\xE9\x81\x93"
#define TXT_USB_WAIT    "USB CDC Shell \xE7\xAD\x89\xE5\xBE\x85\xE8\xBE\x93\xE5\x85\xA5"
#define TXT_SCREEN      "\xE5\xB1\x8F\xE5\xB9\x95\xE5\x88\xB7\xE6\x96\xB0"
#define TXT_LVGL_OK     "LVGL \xE9\xA1\xB5\xE9\x9D\xA2\xE5\x88\xB7\xE6\x96\xB0\xE7\xA8\xB3\xE5\xAE\x9A"
#define TXT_DEVICE      "\xE4\xB9\x90\xE5\xA4\x9A\xE5\xB1\x8F\xE6\x8E\xA7"
#define TXT_FW          "\xE6\xBC\x94\xE7\xA4\xBA\xE7\x89\x88 0.2"

static sim_data_t s_data;
static bool s_initialized = false;

static int rand_range(int lo, int hi)
{
    if(hi <= lo) {
        return lo;
    }
    return lo + (rand() % (hi - lo + 1));
}

static void push_chart_point(uint8_t cpu, uint8_t mem, uint8_t net)
{
    for(int i = 0; i < SIM_CHART_POINTS - 1; i++) {
        s_data.chart_cpu[i] = s_data.chart_cpu[i + 1];
        s_data.chart_mem[i] = s_data.chart_mem[i + 1];
        s_data.chart_net[i] = s_data.chart_net[i + 1];
    }

    s_data.chart_cpu[SIM_CHART_POINTS - 1] = cpu;
    s_data.chart_mem[SIM_CHART_POINTS - 1] = mem;
    s_data.chart_net[SIM_CHART_POINTS - 1] = net;
}

void sim_data_init(void)
{
    memset(&s_data, 0, sizeof(s_data));

    s_data.cpu_usage = 32;
    s_data.memory_usage = 45;
    s_data.storage_usage = 61;
    s_data.temperature = 36.8f;

    s_data.nearlink.connected = true;
    s_data.nearlink.rssi = -46;
    strcpy(s_data.nearlink.status_text, TXT_ONLINE);

    s_data.w800_at.connected = true;
    s_data.w800_at.rssi = -53;
    strcpy(s_data.w800_at.status_text, TXT_READY);

    s_data.modbus.connected = true;
    s_data.modbus.stations = 3;
    strcpy(s_data.modbus.status_text, TXT_RUNNING);

    s_data.mqtt.connected = true;
    s_data.mqtt.qos = 1;
    strcpy(s_data.mqtt.status_text, TXT_SYNC);

    s_data.msg_per_sec = 128;
    s_data.msg_published = 21420;
    s_data.msg_processed = 21391;
    s_data.msg_dropped = 7;
    s_data.msg_subscribers = 6;

    for(int i = 0; i < SIM_CHART_POINTS; i++) {
        s_data.chart_cpu[i] = (uint8_t)(26 + (i % 12));
        s_data.chart_mem[i] = (uint8_t)(42 + (i % 9));
        s_data.chart_net[i] = (uint8_t)(18 + ((i * 3) % 20));
    }
    s_data.chart_count = SIM_CHART_POINTS;

    s_data.alert_count = 3;
    s_data.alerts[0].level = SIM_ALERT_INFO;
    strcpy(s_data.alerts[0].title, TXT_UART_ECHO);
    strcpy(s_data.alerts[0].message, TXT_UART_MSG);
    strcpy(s_data.alerts[0].timestamp, "12:28:46");

    s_data.alerts[1].level = SIM_ALERT_WARNING;
    strcpy(s_data.alerts[1].title, TXT_CMD_CH);
    strcpy(s_data.alerts[1].message, TXT_USB_WAIT);
    strcpy(s_data.alerts[1].timestamp, "12:29:11");

    s_data.alerts[2].level = SIM_ALERT_INFO;
    strcpy(s_data.alerts[2].title, TXT_SCREEN);
    strcpy(s_data.alerts[2].message, TXT_LVGL_OK);
    strcpy(s_data.alerts[2].timestamp, "12:29:24");

    strcpy(s_data.device_model, TXT_DEVICE);
    strcpy(s_data.firmware_version, TXT_FW);
    s_data.uptime_seconds = 9 * 3600 + 23 * 60 + 18;

    s_data.year = 2026;
    s_data.month = 7;
    s_data.day = 3;
    s_data.hour = 12;
    s_data.minute = 30;
    s_data.second = 0;

    s_data.system_normal = true;
    s_initialized = true;
    srand(231);
}

void sim_data_tick(void)
{
    if(!s_initialized) {
        return;
    }

    s_data.second++;
    if(s_data.second >= 60) {
        s_data.second = 0;
        s_data.minute++;
        if(s_data.minute >= 60) {
            s_data.minute = 0;
            s_data.hour = (s_data.hour + 1) % 24;
        }
    }

    s_data.uptime_seconds++;

    s_data.cpu_usage = (uint8_t)rand_range(24, 55);
    s_data.memory_usage = (uint8_t)rand_range(38, 64);
    s_data.storage_usage = (uint8_t)rand_range(59, 66);
    s_data.temperature = 33.0f + ((float)rand_range(0, 80) / 10.0f);

    s_data.nearlink.rssi = (int8_t)rand_range(-58, -39);
    s_data.w800_at.rssi = (int8_t)rand_range(-66, -45);
    s_data.modbus.stations = (uint8_t)rand_range(3, 5);
    s_data.mqtt.qos = (uint8_t)rand_range(0, 1);

    s_data.msg_per_sec = (uint32_t)rand_range(92, 176);
    s_data.msg_published += s_data.msg_per_sec;
    s_data.msg_processed += s_data.msg_per_sec - (uint32_t)rand_range(0, 2);
    if((rand() % 10) == 0) {
        s_data.msg_dropped++;
    }

    push_chart_point(s_data.cpu_usage,
                     s_data.memory_usage,
                     (uint8_t)rand_range(16, 68));
}

const sim_data_t *sim_data_get(void)
{
    return &s_data;
}
