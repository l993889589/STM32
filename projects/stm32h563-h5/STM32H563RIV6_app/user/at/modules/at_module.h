/*
 * at_module.h
 *
 * Purpose:
 *   Defines the common AT module driver facade used by WiFi and cellular
 *   modules. It keeps protocol users independent from module-specific command
 *   names while still exposing explicit socket IDs for multi-socket modules.
 *
 * Usage:
 *   Initialize at_module_t with one at_session_t and one driver table. Use the
 *   *_socket_id APIs when MQTT control traffic and HTTP data traffic use
 *   separate modem sockets. Single-socket compatibility wrappers remain for
 *   older users that store the active socket in module->socket_id.
 *
 * Constraints:
 *   Socket IDs are modem-side channels only. All commands still share the same
 *   serialized AT UART session, so callers must not issue concurrent commands
 *   through different IDs.
 */
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
    bool (*open_socket)(at_module_t *module, const at_socket_config_t *config, int *socket_id);
    bool (*send_socket)(at_module_t *module, int socket_id, const uint8_t *data, uint16_t len, uint16_t *actual_len);
    bool (*recv_socket)(at_module_t *module, int socket_id, uint8_t *data, uint16_t max_len, uint16_t *actual_len);
    bool (*close_socket)(at_module_t *module, int socket_id);
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

/* Open a socket and return the module-specific socket id without changing
 * module->socket_id. Use this for modules that support more than one active
 * socket, such as an MQTT control socket plus a separate HTTP data socket.
 */
bool at_module_open_socket_id(at_module_t *module, const at_socket_config_t *config, int *socket_id);

/* Compatibility wrapper for single-socket users. On success it stores the id
 * returned by at_module_open_socket_id() in module->socket_id.
 */
bool at_module_open_socket(at_module_t *module, const at_socket_config_t *config);

/* Send data through an explicit socket id. actual_len is optional and reports
 * the module-accepted byte count when the underlying AT command exposes it.
 */
bool at_module_send_socket_id(at_module_t *module, int socket_id, const uint8_t *data, uint16_t len, uint16_t *actual_len);

/* Compatibility wrapper that sends through module->socket_id. */
bool at_module_send_socket(at_module_t *module, const uint8_t *data, uint16_t len);

/* Receive up to max_len bytes from an explicit socket id. Drivers must return
 * only network payload bytes here; AT response headers must not be included.
 */
bool at_module_recv_socket_id(at_module_t *module, int socket_id, uint8_t *data, uint16_t max_len, uint16_t *actual_len);

/* Close an explicit socket id without modifying module->socket_id. */
bool at_module_close_socket_id(at_module_t *module, int socket_id);

/* Compatibility wrapper that closes module->socket_id and clears it. */
bool at_module_close_socket(at_module_t *module);

#endif /* AT_MODULE_H */
