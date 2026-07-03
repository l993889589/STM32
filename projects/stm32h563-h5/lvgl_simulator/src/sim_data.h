#ifndef SIM_DATA_H
#define SIM_DATA_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum number of alerts */
#define SIM_MAX_ALERTS 8

/* Alert severity */
typedef enum {
    SIM_ALERT_INFO = 0,
    SIM_ALERT_WARNING = 1,
    SIM_ALERT_ERROR = 2
} sim_alert_level_t;

/* Single alert entry */
typedef struct {
    sim_alert_level_t level;
    char title[32];
    char message[64];
    char timestamp[16];  /* HH:MM:SS */
} sim_alert_t;

/* Communication protocol status */
typedef struct {
    bool connected;
    int8_t rssi;          /* dBm, -128 if N/A */
    uint8_t qos;          /* MQTT QOS */
    uint8_t stations;     /* Modbus station count */
    char status_text[16]; /* e.g. "已连接", "运行中" */
} sim_comm_status_t;

/* Complete data snapshot */
typedef struct {
    /* System status */
    uint8_t cpu_usage;       /* percent */
    uint8_t memory_usage;    /* percent */
    uint8_t storage_usage;   /* percent */
    float temperature;       /* Celsius */

    /* Communication */
    sim_comm_status_t nearlink;
    sim_comm_status_t w800_at;
    sim_comm_status_t modbus;
    sim_comm_status_t mqtt;

    /* Message bus */
    uint32_t msg_per_sec;
    uint32_t msg_published;
    uint32_t msg_processed;
    uint32_t msg_dropped;
    uint8_t msg_subscribers;

    /* Real-time chart history (last N points) */
    #define SIM_CHART_POINTS 40
    uint8_t chart_cpu[SIM_CHART_POINTS];
    uint8_t chart_mem[SIM_CHART_POINTS];
    uint8_t chart_net[SIM_CHART_POINTS];
    uint8_t chart_count;     /* how many valid points */

    /* Alerts */
    sim_alert_t alerts[SIM_MAX_ALERTS];
    uint8_t alert_count;

    /* System info */
    char device_model[32];
    char firmware_version[16];
    uint32_t uptime_seconds;
    int year, month, day;
    int hour, minute, second;

    /* Overall system status */
    bool system_normal;
} sim_data_t;

/* Get a pointer to the live data (updated by sim_data_tick) */
const sim_data_t *sim_data_get(void);

/* Advance simulation by one tick (call every ~500ms) */
void sim_data_tick(void);

/* Initialize the data model with reference-image values */
void sim_data_init(void);

#endif /* SIM_DATA_H */
