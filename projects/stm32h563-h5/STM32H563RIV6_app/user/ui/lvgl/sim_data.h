/**
 * @file sim_data.h
 * @brief UI telemetry, alert, chart, and storage-state data model.
 */

#ifndef SIM_DATA_H
#define SIM_DATA_H

#include <stdbool.h>
#include <stdint.h>

#define SIM_MAX_ALERTS          8U
#define SIM_CHART_POINTS        40U
#define SIM_DISK_COUNT          6U

typedef enum
{
    SIM_ALERT_INFO = 0,
    SIM_ALERT_WARNING = 1,
    SIM_ALERT_ERROR = 2
} sim_alert_level_t;

typedef struct
{
    sim_alert_level_t level;
    char title[32];
    char message[64];
    char timestamp[16];
} sim_alert_t;

typedef struct
{
    bool connected;
    int8_t rssi;
    uint8_t qos;
    uint8_t stations;
    char status_text[16];
} sim_comm_status_t;

typedef struct
{
    uint8_t cpu_usage;
    uint8_t gpu_usage;
    uint8_t memory_usage;
    uint8_t storage_usage;
    uint8_t fan_load;
    uint8_t humidity;
    uint16_t cpu_freq_mhz;
    uint16_t board_voltage_mv;
    float cpu_temperature;
    float gpu_temperature;
    float rear_temperature;
    float front_temperature;
    float temperature;

    sim_comm_status_t nearlink;
    sim_comm_status_t w800_at;
    sim_comm_status_t modbus;
    sim_comm_status_t mqtt;

    uint32_t msg_per_sec;
    uint32_t msg_published;
    uint32_t msg_processed;
    uint32_t msg_dropped;
    uint8_t msg_subscribers;

    uint8_t chart_cpu[SIM_CHART_POINTS];
    uint8_t chart_mem[SIM_CHART_POINTS];
    uint8_t chart_net[SIM_CHART_POINTS];
    uint8_t chart_count;

    uint8_t disk_health[SIM_DISK_COUNT];
    sim_alert_t alerts[SIM_MAX_ALERTS];
    uint8_t alert_count;

    char device_model[32];
    char firmware_version[16];
    uint32_t uptime_seconds;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;

    bool system_normal;
} sim_data_t;

const sim_data_t *sim_data_get(void);
void sim_data_tick(void);
void sim_data_init(void);

#endif /* SIM_DATA_H */
