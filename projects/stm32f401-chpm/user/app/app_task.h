/**
 * @file app_task.h
 * @brief Public application task commands and parameter submission.
 */

#ifndef APP_TASK_H
#define APP_TASK_H

#include "param.h"

#define BIT_0    (1UL << 0)
#define BIT_1    (1UL << 1)
#define BIT_STOP (1UL << 2)

extern volatile float ds18b20_temp;

/** @brief Runtime diagnostics for the single-owner parameter flash service. */
typedef struct
{
    uint32_t success_count;
    uint32_t failure_count;
    uint32_t busy_count;
    uint32_t timeout_count;
    uint32_t spare_prepare_success_count;
    uint32_t spare_prepare_failure_count;
    param_store_status_t last_result;
    uint8_t busy;
} app_param_store_health_t;

/**
 * @brief Validate and submit one parameter snapshot with a bounded wait.
 * @param param Candidate application parameter values.
 * @return true only when durability is confirmed within the response window.
 */
bool ParamQueueSend(PARAM_T param);

/**
 * @brief Copy a non-blocking persistence health snapshot.
 * @param health Destination for counters, state, and the latest flash result.
 */
void app_param_store_get_health(app_param_store_health_t *health);

/** @brief Latch the reliable periodic screen buzzer schedule on. */
void app_set_alarm(void);

/** @brief Cancel the periodic screen buzzer schedule. */
void app_set_alarm_stop(void);

#endif /* APP_TASK_H */
