#ifndef APP_TASK_H
#define APP_TASK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "param.h"

#define BIT_0    (1UL << 0)
#define BIT_1    (1UL << 1)
#define BIT_STOP (1UL << 2)

typedef enum
{
    DEVICE_AHT20 = 1,
    DEVICE_DS18B20,
    DEVICE_PWM,
    DEVICE_ALARM,
    DEVICE_FORWARD,
    DEVICE_PAGE,
    DEVICE_TEXT,
    DEVICE_COLOR,
    DEVICE_RST,
    DEVICE_CONNECT,
    DEVICE_INIT,
    TEMP_ALARM,
    TEMP_COLOR
} DeviceType;

extern volatile float ds18b20_temp;

bool enqueue_data(const uint8_t *data, size_t length);
void ParamQueueSend(PARAM_T param);
void app_set_alarm(void);
void app_set_alarm_stop(void);
void set_event_bits(uint32_t bits);

#endif
