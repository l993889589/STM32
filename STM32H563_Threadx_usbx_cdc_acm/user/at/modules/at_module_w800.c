#include "at_module_w800.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"

#define W800_CMD_TIMEOUT_MS       3000U
#define W800_JOIN_TIMEOUT_MS      45000U

static void w800_log(at_module_t *module, const char *line)
{
    if(module && module->session && module->session->log && line)
        module->session->log(line, module->session->log_arg);
}

static bool w800_probe(at_module_t *module)
{
    w800_log(module, "w800 state: probe");

    if(at_session_cmd_expect(module->session, "AT+", "+OK", 1000U, 5U))
        return true;

    w800_log(module, "w800 state: probe compatible AT");
    return at_session_cmd_expect(module->session, "AT", "+OK", 1000U, 5U);
}

static bool w800_reset(at_module_t *module)
{
    w800_log(module, "w800 state: reset");
    bsp_w800_hard_reset(100U, 3000U);
    module->socket_id = -1;

    return true;
}

static bool w800_wifi_is_ready(at_module_t *module)
{
    if(!at_session_cmd_expect(module->session, "AT+LKSTT", "+OK", W800_CMD_TIMEOUT_MS, 1U))
        return false;

    return strstr(at_session_capture(module->session), "+OK=1") != NULL;
}

static bool w800_scan_wifi(at_module_t *module, const char *ssid)
{
    bool found;

    if(!ssid)
        return false;

    w800_log(module, "w800 state: scan wifi");
    at_session_clear_capture(module->session);

    if(at_client_send(&module->session->client,
                      "AT+WSCAN=3FFF,2,120",
                      at_session_now_ms(module->session),
                      12000U) != 0)
        return false;

    if(!at_session_wait_contains(module->session, "+OK", 12000U))
    {
        w800_log(module, "w800 warn: wifi scan timeout");
        at_client_clear_result(&module->session->client);
        return false;
    }

    if(module->session->sleep_ms)
        module->session->sleep_ms(1000U, module->session->sleep_arg);
    at_session_poll_input(module->session);

    found = strstr(at_session_capture(module->session), ssid) != NULL;
    w800_log(module, found ? "w800 state: configured ssid found" :
                             "w800 warn: configured ssid not found in scan");

    at_client_clear_result(&module->session->client);
    return found;
}

static bool w800_wait_wifi_ready(at_module_t *module, uint32_t timeout_ms)
{
    uint32_t waited = 0U;

    while(waited < timeout_ms)
    {
        if(w800_wifi_is_ready(module))
            return true;

        if(module->session->sleep_ms)
            module->session->sleep_ms(1000U, module->session->sleep_arg);
        waited += 1000U;
    }

    return false;
}

static bool w800_connect_network(at_module_t *module, const void *config)
{
    const at_wifi_config_t *wifi = (const at_wifi_config_t *)config;
    char cmd[128];

    if(!wifi || !wifi->ssid || !wifi->password)
        return false;

    if(!w800_probe(module))
        return false;

    w800_log(module, "w800 state: config sta");
    if(!at_session_cmd_expect(module->session, "AT+WPRT=0", "+OK", W800_CMD_TIMEOUT_MS, 2U))
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+SSID=\"%s\"", wifi->ssid);
    if(!at_session_cmd_expect(module->session, cmd, "+OK", W800_CMD_TIMEOUT_MS, 2U))
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+KEY=1,0,\"%s\"", wifi->password);
    if(!at_session_cmd_expect(module->session, cmd, "+OK", W800_CMD_TIMEOUT_MS, 2U))
        return false;

    if(!at_session_cmd_expect(module->session, "AT+NIP=0", "+OK", W800_CMD_TIMEOUT_MS, 2U))
        return false;

    w800_log(module, "w800 state: save wifi profile");
    if(!at_session_cmd_expect(module->session, "AT+PMTF", "+OK", W800_CMD_TIMEOUT_MS, 2U))
        w800_log(module, "w800 warn: save wifi profile failed");

    w800_log(module, "w800 state: restart after profile save");
    (void)at_session_cmd_expect(module->session, "AT+Z", "+OK", 1000U, 1U);

    if(module->session->sleep_ms)
        module->session->sleep_ms(1500U, module->session->sleep_arg);

    if(!w800_probe(module))
        return false;

    (void)w800_scan_wifi(module, wifi->ssid);

    w800_log(module, "w800 state: join wifi");
    if(!at_session_cmd_expect(module->session, "AT+WJOIN", "+OK", W800_JOIN_TIMEOUT_MS, 1U))
        return false;

    w800_log(module, "w800 state: wait dhcp");
    return w800_wait_wifi_ready(module, 15000U);
}

static int w800_parse_socket(const char *capture)
{
    const char *ok = strstr(capture ? capture : "", "+OK=");

    if(!ok)
        return -1;

    ok += 4;
    if(*ok < '0' || *ok > '9')
        return -1;

    return *ok - '0';
}

static bool w800_open_socket(at_module_t *module, const at_socket_config_t *config)
{
    char cmd[128];

    if(!config || !config->host || config->port == 0U)
        return false;

    w800_log(module, "w800 state: tcp socket");
    (void)snprintf(cmd, sizeof(cmd),
                   "AT+SKCT=0,0,\"%s\",%u,%u",
                   config->host,
                   (unsigned int)config->port,
                   (unsigned int)config->local_port);

    at_session_clear_capture(module->session);
    if(!at_session_cmd_expect(module->session, cmd, "+OK=", 8000U, 1U))
        return false;

    module->socket_id = w800_parse_socket(at_session_capture(module->session));
    return module->socket_id >= 0;
}

static bool w800_send_socket(at_module_t *module, const uint8_t *data, uint16_t len)
{
    char cmd[64];

    if(module->socket_id < 0 || !data || len == 0U)
        return false;

    at_session_clear_capture(module->session);
    (void)snprintf(cmd, sizeof(cmd), "AT+SKSND=%d,%u", module->socket_id, (unsigned int)len);

    if(at_client_send(&module->session->client,
                      cmd,
                      at_session_now_ms(module->session),
                      W800_CMD_TIMEOUT_MS) != 0)
        return false;

    if(module->session->sleep_ms)
        module->session->sleep_ms(20U, module->session->sleep_arg);

    if(at_session_send_raw(module->session, data, len) != 0)
    {
        at_client_clear_result(&module->session->client);
        return false;
    }

    if(at_session_wait_contains(module->session, "+OK", W800_CMD_TIMEOUT_MS))
    {
        at_client_clear_result(&module->session->client);
        return true;
    }

    at_client_clear_result(&module->session->client);
    return false;
}

static bool w800_close_socket(at_module_t *module)
{
    char cmd[32];

    if(!module || module->socket_id < 0)
        return true;

    (void)snprintf(cmd, sizeof(cmd), "AT+SKCLS=%d", module->socket_id);
    (void)at_session_cmd_expect(module->session, cmd, "+OK", 1000U, 1U);
    module->socket_id = -1;

    return true;
}

const at_module_driver_t g_at_module_w800 =
{
    "W800",
    AT_MODULE_KIND_WIFI,
    w800_probe,
    w800_reset,
    w800_connect_network,
    w800_wifi_is_ready,
    w800_open_socket,
    w800_send_socket,
    w800_close_socket
};
