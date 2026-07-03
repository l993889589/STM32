#ifndef AT_MODULE_NEARLINK_H
#define AT_MODULE_NEARLINK_H

#include <stdbool.h>
#include <stdint.h>

#include "at_session.h"

typedef enum
{
    AT_NEARLINK_ROLE_CLIENT = 0,
    AT_NEARLINK_ROLE_SERVER = 1
} at_nearlink_role_t;

typedef struct
{
    at_nearlink_role_t role;
    const char *local_name;
    const char *local_address;
    const char *peer_name;
    uint8_t auth_type;
    const char *key;
} at_nearlink_config_t;

typedef void (*at_nearlink_reset_cb_t)(void *arg);
typedef void (*at_nearlink_data_cb_t)(const char *peer, const uint8_t *data, uint16_t len, void *arg);

typedef struct
{
    at_session_t *session;
    at_nearlink_role_t role;
    at_nearlink_reset_cb_t reset;
    void *reset_arg;
    at_nearlink_data_cb_t data;
    void *data_arg;
    uint8_t active;
    uint8_t connected;
    const char *last_error;
} at_nearlink_module_t;

void at_nearlink_init(at_nearlink_module_t *module, at_session_t *session,
                      at_nearlink_reset_cb_t reset, void *reset_arg,
                      at_nearlink_data_cb_t data, void *data_arg);
bool at_nearlink_probe(at_nearlink_module_t *module);
bool at_nearlink_apply(at_nearlink_module_t *module, const at_nearlink_config_t *config);
bool at_nearlink_stop(at_nearlink_module_t *module);
bool at_nearlink_send(at_nearlink_module_t *module, const uint8_t *data, uint16_t len);

#endif /* AT_MODULE_NEARLINK_H */
