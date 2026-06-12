#include "at_module_ec20.h"

#include <stdio.h>
#include <string.h>

#define EC20_CMD_TIMEOUT_MS       3000U
#define EC20_NET_TIMEOUT_MS       60000U
#define EC20_SOCKET_TIMEOUT_MS    30000U

static void ec20_log(at_module_t *module, const char *line)
{
    if(module && module->session && module->session->log && line)
        module->session->log(line, module->session->log_arg);
}

static bool ec20_probe(at_module_t *module)
{
    ec20_log(module, "ec20 state: probe");
    return at_session_cmd_expect(module->session, "AT", "OK", 1000U, 5U);
}

static bool ec20_reset(at_module_t *module)
{
    ec20_log(module, "ec20 state: reset");

    if(!at_session_cmd_expect(module->session, "AT+CFUN=1,1", "OK", EC20_CMD_TIMEOUT_MS, 1U))
        return false;

    if(module->session->sleep_ms)
        module->session->sleep_ms(5000U, module->session->sleep_arg);

    module->socket_id = -1;
    return ec20_probe(module);
}

static bool ec20_is_network_ready(at_module_t *module)
{
    if(!at_session_cmd_expect(module->session, "AT+CGATT?", "+CGATT: 1", EC20_CMD_TIMEOUT_MS, 1U))
        return false;

    return true;
}

static bool ec20_wait_network(at_module_t *module, uint32_t timeout_ms)
{
    uint32_t waited = 0U;

    while(waited < timeout_ms)
    {
        if(ec20_is_network_ready(module))
            return true;

        if(module->session->sleep_ms)
            module->session->sleep_ms(1000U, module->session->sleep_arg);
        waited += 1000U;
    }

    return false;
}

static bool ec20_connect_network(at_module_t *module, const void *config)
{
    const at_cellular_config_t *cell = (const at_cellular_config_t *)config;
    char cmd[160];

    if(!cell || !cell->apn)
        return false;

    if(!ec20_probe(module))
        return false;

    if(!at_session_cmd_expect(module->session, "ATE0", "OK", EC20_CMD_TIMEOUT_MS, 2U))
        return false;

    if(!at_session_cmd_expect(module->session, "AT+CPIN?", "READY", EC20_CMD_TIMEOUT_MS, 10U))
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",1",
                   cell->apn,
                   cell->user ? cell->user : "",
                   cell->password ? cell->password : "");
    if(!at_session_cmd_expect(module->session, cmd, "OK", EC20_CMD_TIMEOUT_MS, 2U))
        return false;

    if(!at_session_cmd_expect(module->session, "AT+QIACT=1", "OK", EC20_NET_TIMEOUT_MS, 1U))
        return false;

    return ec20_wait_network(module, EC20_NET_TIMEOUT_MS);
}

static bool ec20_open_socket(at_module_t *module, const at_socket_config_t *config)
{
    char cmd[160];

    if(!config || !config->host || config->port == 0U)
        return false;

    module->socket_id = 0;
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QIOPEN=1,%d,\"TCP\",\"%s\",%u,0,0",
                   module->socket_id,
                   config->host,
                   (unsigned int)config->port);

    if(!at_session_cmd_expect(module->session, cmd, "OK", EC20_SOCKET_TIMEOUT_MS, 1U))
        return false;

    return at_session_wait_contains(module->session, "+QIOPEN: 0,0", EC20_SOCKET_TIMEOUT_MS);
}

static bool ec20_send_socket(at_module_t *module, const uint8_t *data, uint16_t len)
{
    char cmd[64];

    if(module->socket_id < 0 || !data || len == 0U)
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+QISEND=%d,%u", module->socket_id, (unsigned int)len);
    if(!at_session_cmd_expect(module->session, cmd, ">", EC20_CMD_TIMEOUT_MS, 1U))
        return false;

    if(at_session_send_raw(module->session, data, len) != 0)
        return false;

    return at_session_wait_contains(module->session, "SEND OK", EC20_SOCKET_TIMEOUT_MS);
}

static bool ec20_close_socket(at_module_t *module)
{
    char cmd[32];

    if(!module || module->socket_id < 0)
        return true;

    (void)snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%d", module->socket_id);
    (void)at_session_cmd_expect(module->session, cmd, "OK", EC20_CMD_TIMEOUT_MS, 1U);
    module->socket_id = -1;

    return true;
}

const at_module_driver_t g_at_module_ec20 =
{
    "EC20",
    AT_MODULE_KIND_CELLULAR,
    ec20_probe,
    ec20_reset,
    ec20_connect_network,
    ec20_is_network_ready,
    ec20_open_socket,
    ec20_send_socket,
    ec20_close_socket
};
