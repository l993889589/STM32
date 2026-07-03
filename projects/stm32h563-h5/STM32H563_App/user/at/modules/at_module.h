#ifndef AT_MODULE_H
#define AT_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#include "at_session.h"

typedef enum
{
    AT_MODULE_KIND_WIFI = 0,
    AT_MODULE_KIND_CELLULAR
} at_module_kind_t;

typedef struct
{
    const char *ssid;
    const char *password;
} at_wifi_config_t;

typedef struct
{
    const char *apn;
    const char *user;
    const char *password;
} at_cellular_config_t;

typedef struct
{
    const char *host;
    uint16_t port;
    uint16_t local_port;
} at_socket_config_t;

typedef struct at_module_driver at_module_driver_t;

typedef struct
{
    at_session_t *session;
    const at_module_driver_t *driver;
    int socket_id;
    void *driver_arg;
} at_module_t;

struct at_module_driver
{
    const char *name;
    at_module_kind_t kind;
    bool (*probe)(at_module_t *module);
    bool (*reset)(at_module_t *module);
    bool (*connect_network)(at_module_t *module, const void *config);
    bool (*is_network_ready)(at_module_t *module);
    bool (*open_socket)(at_module_t *module, const at_socket_config_t *config);
    bool (*send_socket)(at_module_t *module, const uint8_t *data, uint16_t len);
    bool (*close_socket)(at_module_t *module);
};

void at_module_init(at_module_t *module,
                    at_session_t *session,
                    const at_module_driver_t *driver,
                    void *driver_arg);
const char *at_module_name(const at_module_t *module);
bool at_module_probe(at_module_t *module);
bool at_module_reset(at_module_t *module);
bool at_module_connect_network(at_module_t *module, const void *config);
bool at_module_is_network_ready(at_module_t *module);
bool at_module_open_socket(at_module_t *module, const at_socket_config_t *config);
bool at_module_send_socket(at_module_t *module, const uint8_t *data, uint16_t len);
bool at_module_close_socket(at_module_t *module);

#endif /* AT_MODULE_H */
