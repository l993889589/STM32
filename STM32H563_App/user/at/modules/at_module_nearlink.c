#include "at_module_nearlink.h"

#include <stdio.h>
#include <string.h>

#define NEARLINK_CMD_TIMEOUT_MS  3000U
#define NEARLINK_CONNECT_TIMEOUT_MS  10000U
#define NEARLINK_MAX_DATA        256U

static int nearlink_hex_value(char ch)
{
    if(ch >= '0' && ch <= '9') return ch - '0';
    if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void nearlink_rx_urc(const char *line, void *arg)
{
    at_nearlink_module_t *module = (at_nearlink_module_t *)arg;
    uint8_t data[NEARLINK_MAX_DATA];
    char peer[32] = {0};
    const char *hex;
    const char *comma;
    uint16_t len = 0U;

    if(!module || !line)
        return;

    if(strncmp(line, "+SRECVDATA:", 11U) == 0)
    {
        const char *value = line + 11U;
        while(*value == ' ') value++;
        comma = strchr(value, ',');
        if(!comma) return;
        {
            size_t peer_len = (size_t)(comma - value);
            if(peer_len >= sizeof(peer)) peer_len = sizeof(peer) - 1U;
            memcpy(peer, value, peer_len);
        }
        hex = comma + 1U;
    }
    else
    {
        hex = strchr(line, ':');
        if(!hex) return;
        hex++;
        while(*hex == ' ') hex++;
    }

    while(hex[0] && hex[1] && len < sizeof(data))
    {
        int high = nearlink_hex_value(hex[0]);
        int low = nearlink_hex_value(hex[1]);
        if(high < 0 || low < 0) break;
        data[len++] = (uint8_t)((high << 4) | low);
        hex += 2;
    }

    if(len != 0U && module->data)
        module->data(peer[0] ? peer : NULL, data, len, module->data_arg);
}

void at_nearlink_init(at_nearlink_module_t *module, at_session_t *session,
                      at_nearlink_reset_cb_t reset, void *reset_arg,
                      at_nearlink_data_cb_t data, void *data_arg)
{
    if(!module) return;
    memset(module, 0, sizeof(*module));
    module->session = session;
    module->reset = reset;
    module->reset_arg = reset_arg;
    module->data = data;
    module->data_arg = data_arg;
    if(session)
    {
        (void)at_client_register_urc(&session->client, "+SRECVDATA:", nearlink_rx_urc, module);
        (void)at_client_register_urc(&session->client, "+CRECVDATA", nearlink_rx_urc, module);
    }
}

bool at_nearlink_probe(at_nearlink_module_t *module)
{
    return module && module->session &&
           at_session_cmd_expect(module->session, "AT", "OK", 1000U, 3U);
}

static bool nearlink_cmd(at_nearlink_module_t *module, const char *cmd)
{
    return at_session_cmd_expect(module->session, cmd, "OK", NEARLINK_CMD_TIMEOUT_MS, 2U);
}

bool at_nearlink_stop(at_nearlink_module_t *module)
{
    bool ok;
    if(!module || !module->session) return false;
    if(!module->active) return true;
    ok = nearlink_cmd(module, module->role == AT_NEARLINK_ROLE_SERVER ?
                              "AT+SSERVER=0" : "AT+CDISCONNECT");
    module->active = 0U;
    module->connected = 0U;
    return ok;
}

bool at_nearlink_apply(at_nearlink_module_t *module, const at_nearlink_config_t *config)
{
    char cmd[128];
    if(!module || !module->session || !config || !config->local_name) return false;
    if(module->reset) module->reset(module->reset_arg);
    if(!at_nearlink_probe(module)) return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+SETMODE=%u", (unsigned int)config->role);
    if(!nearlink_cmd(module, cmd)) return false;
    if(config->local_address && config->local_address[0])
    {
        (void)snprintf(cmd, sizeof(cmd), "AT+SETSLEADDR=%s", config->local_address);
        if(!nearlink_cmd(module, cmd)) return false;
    }

    module->role = config->role;
    module->connected = 0U;
    if(config->role == AT_NEARLINK_ROLE_SERVER)
    {
        (void)snprintf(cmd, sizeof(cmd), "AT+SSETNAME=%s", config->local_name);
        if(!nearlink_cmd(module, cmd)) return false;
    }
    else
    {
        if(!config->peer_name || !config->peer_name[0]) return false;
        (void)snprintf(cmd, sizeof(cmd), "AT+CSETNAME=%s", config->local_name);
        if(!nearlink_cmd(module, cmd)) return false;
    }

    /* Address/name settings are persistent and some firmware revisions only
     * apply them after reboot. Re-probe before starting advertising/scanning. */
    if(module->reset) module->reset(module->reset_arg);
    if(!at_nearlink_probe(module)) return false;

    if(config->role == AT_NEARLINK_ROLE_SERVER)
    {
        if(config->auth_type != 0U && config->key && config->key[0])
            (void)snprintf(cmd, sizeof(cmd), "AT+SSERVER=1,%u,%s", config->auth_type, config->key);
        else
            (void)snprintf(cmd, sizeof(cmd), "AT+SSERVER=1");
        if(!nearlink_cmd(module, cmd)) return false;
    }
    else
    {
        (void)nearlink_cmd(module, "AT+CSLIST");
        if(config->auth_type != 0U && config->key && config->key[0])
            (void)snprintf(cmd, sizeof(cmd), "AT+CCONNECT=%s,%u,%s", config->peer_name, config->auth_type, config->key);
        else
            (void)snprintf(cmd, sizeof(cmd), "AT+CCONNECT=%s", config->peer_name);
        if(!at_session_cmd_expect(module->session, cmd, "OK", NEARLINK_CONNECT_TIMEOUT_MS, 2U)) return false;
        module->connected = 1U;
    }
    module->active = 1U;
    return true;
}

bool at_nearlink_send(at_nearlink_module_t *module, const uint8_t *data, uint16_t len)
{
    char cmd[16U + NEARLINK_MAX_DATA * 2U];
    const char *prefix;
    uint32_t pos;
    static const char hex[] = "0123456789ABCDEF";
    if(!module || !module->session || !module->active || !data || len == 0U || len > NEARLINK_MAX_DATA) return false;
    prefix = module->role == AT_NEARLINK_ROLE_SERVER ? "AT+SSENDALL=" : "AT+CSEND=";
    pos = (uint32_t)strlen(prefix);
    memcpy(cmd, prefix, pos);
    for(uint16_t i = 0U; i < len; i++)
    {
        cmd[pos++] = hex[data[i] >> 4];
        cmd[pos++] = hex[data[i] & 0x0FU];
    }
    cmd[pos] = '\0';
    return nearlink_cmd(module, cmd);
}
