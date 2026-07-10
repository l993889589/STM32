#ifndef APP_OTA_H
#define APP_OTA_H

#include "tx_api.h"
#include <stdint.h>

#define APP_OTA_HEALTH_OBSERVATION_TICKS  60000U
#define APP_OTA_HEARTBEAT_STALE_TICKS     30000U
#define APP_OTA_CONFIRM_DEADLINE_TICKS    120000U
#define APP_OTA_HEALTH_POLL_TICKS         1000U

uint8_t app_ota_confirm_trial_boot(void);
void app_ota_confirm_task_entry(ULONG thread_input);

#endif /* APP_OTA_H */
