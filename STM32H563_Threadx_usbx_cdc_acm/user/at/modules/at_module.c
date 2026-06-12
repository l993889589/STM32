#include "at_module.h"

#include <string.h>

void at_module_init(at_module_t *module,
                    at_session_t *session,
                    const at_module_driver_t *driver,
                    void *driver_arg)
{
    if(!module)
        return;

    module->session = session;
    module->driver = driver;
    module->socket_id = -1;
    module->driver_arg = driver_arg;
}

const char *at_module_name(const at_module_t *module)
{
    if(!module || !module->driver || !module->driver->name)
        return "unknown";

    return module->driver->name;
}

bool at_module_probe(at_module_t *module)
{
    return module && module->driver && module->driver->probe && module->driver->probe(module);
}

bool at_module_reset(at_module_t *module)
{
    return module && module->driver && module->driver->reset && module->driver->reset(module);
}

bool at_module_connect_network(at_module_t *module, const void *config)
{
    return module && module->driver && module->driver->connect_network &&
           module->driver->connect_network(module, config);
}

bool at_module_is_network_ready(at_module_t *module)
{
    return module && module->driver && module->driver->is_network_ready &&
           module->driver->is_network_ready(module);
}

bool at_module_open_socket(at_module_t *module, const at_socket_config_t *config)
{
    return module && module->driver && module->driver->open_socket &&
           module->driver->open_socket(module, config);
}

bool at_module_send_socket(at_module_t *module, const uint8_t *data, uint16_t len)
{
    return module && module->driver && module->driver->send_socket &&
           module->driver->send_socket(module, data, len);
}

bool at_module_close_socket(at_module_t *module)
{
    return module && module->driver && module->driver->close_socket &&
           module->driver->close_socket(module);
}
