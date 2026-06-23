#ifndef APP_NEARLINK_H
#define APP_NEARLINK_H

#include <stdint.h>
#include "at_module_nearlink.h"
#include "tx_api.h"

typedef struct
{
    at_nearlink_role_t role;
    uint8_t active;
    uint8_t connected;
    uint8_t apply_pending;
    char local_name[32];
    char peer_name[32];
    const char *last_error;
} app_nearlink_status_t;

UINT app_nearlink_init(void);
void app_nearlink_task_entry(ULONG thread_input);
int app_nearlink_request_role(at_nearlink_role_t role, const char *local_name, const char *peer_name);
int app_nearlink_request_send(const uint8_t *data, uint16_t len);
void app_nearlink_get_status(app_nearlink_status_t *status);

#endif /* APP_NEARLINK_H */
