#ifndef AT_MODULE_W800_H
#define AT_MODULE_W800_H

#include "at_module.h"

extern const at_module_driver_t g_at_module_w800;

/** @brief Create a W800 TCP listener using the SDK AT V1.1 server mode. */
bool at_module_w800_open_server(at_module_t *module,
                                uint16_t local_port,
                                uint32_t idle_timeout_s,
                                int *listener_socket);

/** @brief Return the first connected child socket reported by a listener. */
bool at_module_w800_accept_client(at_module_t *module,
                                  int listener_socket,
                                  int *client_socket);

#endif /* AT_MODULE_W800_H */
