/*
 * at_module_w800.c
 *
 * W800 模块驱动实现（基于通用 at_module 接口）
 *
 * 概述:
 *  - 目的: 为 W800 WiFi 模块实现探测、复位、BLE 配网、USB 救援参数和 socket 功能，
 *    并将这些功能以 at_module_driver_t 的形式导出供上层使用。
 *  - 设计假设:
 *      * 使用 at_session / at_client 提供的同步命令发送与捕获机制。
 *      * 驱动通过 session 的日志回调输出调试信息（若已注册）。
 *      * 驱动在必要时调用底层 BSP（bsp_w800_hard_reset）进行硬件复位。
 *  - 超时与重试:
 *      * 常用命令超时定义为 W800_CMD_TIMEOUT_MS（3000 ms）
 *
 * 注意:
 *  - 本驱动对 AT 响应的解析较为简单（例如通过 strstr 查找 "+OK" 或 "+OK="），
 *    在面对不同固件版本或非标准响应时可能需要增强解析逻辑。
 *  - 本文件中多数函数在失败时会返回 false，上层可据此决定重试或回退策略。
 */

#include "at_module_w800.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"

/* 命令超时（毫秒） */
#define W800_CMD_TIMEOUT_MS       3000U
#define W800_SOCKET_SEND_MAX      256U
#define W800_SOCKET_SEND_GAP_MS   20U
#define W800_SCAN_TIMEOUT_MS      8000U
#define W800_SCAN_IDLE_MS          250U

static const char *w800_find_capture_line_prefix(const char *capture,
                                                 const char *prefix);

/* w800_log
 *
 * 将驱动内部日志通过 session 的日志回调转发（如果已注册）。
 * 便于统一日志输出，不直接使用 printf。
 */
static void w800_log(at_module_t *module, const char *line)
{
    if(module && module->session && module->session->log && line)
		{
			module->session->log(line, module->session->log_arg);
		}

}

/* w800_probe
 *
 * 探测 W800 模块是否响应 AT 命令。
 * 先尝试发送 "AT+" 并期待 "+OK"（某些固件支持），若失败再尝试标准 "AT"。
 *
 * 返回:
 *  - true: 模块响应且兼容 AT
 *  - false: 未响应
 *
 * 备注:
 *  - 使用 at_session_cmd_expect 进行阻塞式发送与期望匹配，带重试。
 */
static bool w800_probe(at_module_t *module)
{
    w800_log(module, "w800 state: probe");

    if(at_session_cmd_expect(module->session, "AT+", "+OK", 1000U, 5U))
        return true;

    w800_log(module, "w800 state: probe compatible AT");
    return at_session_cmd_expect(module->session, "AT", "+OK", 1000U, 5U);
}

/* w800_reset
 *
 * 对 W800 执行硬件复位（通过 BSP 提供的函数），并重置 socket_id。
 *
 * 返回:
 *  - true: 表示已触发复位（不保证模块已完全就绪）
 */
static bool w800_reset(at_module_t *module)
{
    w800_log(module, "w800 state: reset");
    bsp_w800_hard_reset(100U, 3000U);
    if(!at_session_recover_after_transport_reset(module->session))
        return false;
    module->socket_id = -1;

    return true;
}

/* w800_wifi_is_ready
 *
 * 查询模块的 WiFi 状态（通过 AT+LKSTT），并检查捕获缓冲中是否包含 "+OK=1"。
 *
 * 返回:
 *  - true: WiFi 已就绪（例如已获取 DHCP）
 *  - false: 未就绪或命令失败
 */
static bool w800_wifi_is_ready(at_module_t *module)
{
    const char *tail;

    if(!at_session_cmd_expect(module->session, "AT+LKSTT", "+OK", W800_CMD_TIMEOUT_MS, 1U))
        return false;

    tail = w800_find_capture_line_prefix(at_session_capture(module->session),
                                         "+OK=1");
    return tail != NULL && (*tail == '\n' || *tail == '\0');
}


/** @brief Start the W800 firmware's built-in BLE Wi-Fi provisioner. */
bool at_module_w800_start_ble_provision(at_module_t *module)
{
    if((module == NULL) || (module->session == NULL) || !w800_probe(module))
        return false;

    w800_log(module, "w800 state: start ble provision");
    (void)at_session_cmd_expect(module->session,
                                "AT+ONESHOT=0",
                                "+OK",
                                W800_CMD_TIMEOUT_MS,
                                1U);
    /* BleWiFi requires the combined BT/BLE host profile used by the official
     * WMBleWiFi application. An already-enabled stack may reject BTEN while
     * still accepting ONESHOT=4, so ONESHOT is the authoritative result.
     */
    (void)at_session_cmd_expect(module->session,
                                "AT+BTEN=255,6",
                                "+OK",
                                W800_CMD_TIMEOUT_MS,
                                1U);
    return at_session_cmd_expect(module->session,
                                 "AT+ONESHOT=4",
                                 "+OK",
                                 W800_CMD_TIMEOUT_MS,
                                 2U);
}

/**
 * @brief Stop the W800 firmware's one-shot provisioning service.
 */
bool at_module_w800_stop_provision(at_module_t *module)
{
    if((module == NULL) || (module->session == NULL))
        return false;

    w800_log(module, "w800 state: stop provision");
    return at_session_cmd_expect(module->session,
                                 "AT+ONESHOT=0",
                                 "+OK",
                                 W800_CMD_TIMEOUT_MS,
                                 1U);
}

static bool w800_credential_is_valid(const char *text,
                                     size_t min_length,
                                     size_t max_length)
{
    size_t length;
    size_t i;

    if(text == NULL)
        return false;
    length = strlen(text);
    if(length < min_length || length > max_length)
        return false;

    for(i = 0U; i < length; i++)
    {
        const unsigned char ch = (unsigned char)text[i];

        if(ch < 0x20U || ch > 0x7EU || ch == (unsigned char)'"')
            return false;
    }
    return true;
}

static bool w800_ssid_is_valid(const char *ssid)
{
    size_t length;
    size_t index;

    if(ssid == NULL)
        return false;
    length = strlen(ssid);
    if(length == 0U || length > 32U)
        return false;
    for(index = 0U; index < length; index++)
    {
        const unsigned char ch = (unsigned char)ssid[index];

        if(ch < 0x20U || ch == 0x7FU || ch == (unsigned char)'"' ||
           ch == (unsigned char)'\\')
            return false;
    }
    return true;
}

static void w800_secure_zero(void *data, size_t length)
{
    volatile uint8_t *cursor = (volatile uint8_t *)data;

    while(length-- != 0U)
        *cursor++ = 0U;
}

static bool w800_cmd_expect_secret(at_module_t *module,
                                   const char *command,
                                   const char *expect,
                                   uint32_t timeout_ms)
{
    at_session_log_cb_t saved_log;
    void *saved_log_arg;
    bool accepted;

    saved_log = module->session->log;
    saved_log_arg = module->session->log_arg;
    at_session_set_logger(module->session, NULL, NULL);
    accepted = at_session_cmd_expect(module->session,
                                     command,
                                     expect,
                                     timeout_ms,
                                     1U);
    w800_secure_zero(module->session->capture,
                     sizeof(module->session->capture));
    module->session->capture_len = 0U;
    w800_secure_zero(module->session->client.line_buf,
                     sizeof(module->session->client.line_buf));
    module->session->client.line_len = 0U;
    w800_secure_zero(module->session->client.response,
                     sizeof(module->session->client.response));
    module->session->client.response_len = 0U;
    at_session_set_logger(module->session, saved_log, saved_log_arg);
    return accepted;
}

/** @brief Persist a bounded WPA/WPA2 station profile without logging secrets. */
bool at_module_w800_save_station_profile(at_module_t *module,
                                         const char *ssid,
                                         const char *password)
{
    char command[96];

    if(module == NULL || module->session == NULL ||
       !w800_ssid_is_valid(ssid) ||
       !w800_credential_is_valid(password, 8U, 63U) ||
       !w800_probe(module))
    {
        return false;
    }

    w800_log(module, "w800 state: save usb rescue station profile");
    (void)at_session_cmd_expect(module->session,
                                "AT+ONESHOT=0",
                                "+OK",
                                W800_CMD_TIMEOUT_MS,
                                1U);
    (void)at_session_cmd_expect(module->session,
                                "AT+WLEAV",
                                "+OK",
                                W800_CMD_TIMEOUT_MS,
                                1U);
    if(!at_session_cmd_expect(module->session,
                              "AT+WPRT=0",
                              "+OK",
                              W800_CMD_TIMEOUT_MS,
                              1U))
    {
        return false;
    }

    (void)snprintf(command, sizeof(command), "AT+SSID=\"%s\"", ssid);
    if(!at_session_cmd_expect(module->session,
                              command,
                              "+OK",
                              W800_CMD_TIMEOUT_MS,
                              1U))
    {
        return false;
    }

    (void)snprintf(command, sizeof(command), "AT+KEY=1,0,\"%s\"", password);
    if(!w800_cmd_expect_secret(module,
                               command,
                               "+OK",
                               W800_CMD_TIMEOUT_MS))
    {
        w800_secure_zero(command, sizeof(command));
        return false;
    }
    w800_secure_zero(command, sizeof(command));

    if(!at_session_cmd_expect(module->session,
                              "AT+NIP=0",
                              "+OK",
                              W800_CMD_TIMEOUT_MS,
                              1U))
    {
        return false;
    }

    return at_session_cmd_expect(module->session,
                                 "AT+PMTF",
                                 "+OK",
                                 W800_CMD_TIMEOUT_MS,
                                 1U);
}

static bool w800_parse_scan_line(char *line, at_w800_access_point_t *result)
{
    char *comma1;
    char *comma2;
    char *comma3;
    char *comma4;
    char *last_comma;
    char *ssid_start;
    char *end;
    long channel;
    long encryption;
    long rssi;
    size_t ssid_length;

    if(line == NULL || result == NULL)
        return false;
    if(strncmp(line, "+OK=", 4U) == 0)
        line += 4;
    if(strcmp(line, "AT+WSCAN") == 0 || line[0] == '\0' || line[0] == '+')
        return false;

    comma1 = strchr(line, ',');
    comma2 = comma1 != NULL ? strchr(comma1 + 1, ',') : NULL;
    comma3 = comma2 != NULL ? strchr(comma2 + 1, ',') : NULL;
    comma4 = comma3 != NULL ? strchr(comma3 + 1, ',') : NULL;
    last_comma = comma4 != NULL ? strrchr(comma4 + 1, ',') : NULL;
    if(comma1 == NULL || comma2 == NULL || comma3 == NULL ||
       comma4 == NULL || last_comma == NULL || last_comma == comma4)
        return false;

    channel = strtol(comma2 + 1, &end, 10);
    if(end != comma3 || channel < 1L || channel > 255L)
        return false;
    encryption = strtol(comma3 + 1, &end, 10);
    if(end != comma4 || encryption < 0L || encryption > 255L)
        return false;
    rssi = strtol(last_comma + 1, &end, 10);
    if(end == last_comma + 1)
        return false;
    if(rssi > 0L)
        rssi = -rssi;
    if(rssi < -127L)
        rssi = -127L;
    if(rssi > 0L)
        rssi = 0L;

    ssid_start = comma4 + 1;
    ssid_length = (size_t)(last_comma - ssid_start);
    if(ssid_length >= 2U && ssid_start[0] == '"' &&
       ssid_start[ssid_length - 1U] == '"')
    {
        ssid_start++;
        ssid_length -= 2U;
    }
    if(ssid_length == 0U || ssid_length > 32U)
        return false;
    memcpy(result->ssid, ssid_start, ssid_length);
    result->ssid[ssid_length] = '\0';
    result->channel = (uint8_t)channel;
    result->encryption = (uint8_t)encryption;
    result->rssi_dbm = (int16_t)rssi;
    return true;
}

static void w800_insert_access_point(at_w800_access_point_t *access_points,
                                     uint8_t capacity,
                                     uint8_t *count,
                                     const at_w800_access_point_t *candidate)
{
    uint8_t index;
    uint8_t insert_at;

    for(index = 0U; index < *count; index++)
    {
        if(strcmp(access_points[index].ssid, candidate->ssid) == 0)
        {
            if(candidate->rssi_dbm <= access_points[index].rssi_dbm)
                return;
            while(index + 1U < *count)
            {
                access_points[index] = access_points[index + 1U];
                index++;
            }
            (*count)--;
            break;
        }
    }

    insert_at = 0U;
    while(insert_at < *count &&
          access_points[insert_at].rssi_dbm >= candidate->rssi_dbm)
        insert_at++;
    if(insert_at >= capacity)
        return;
    if(*count < capacity)
        (*count)++;
    for(index = (uint8_t)(*count - 1U); index > insert_at; index--)
        access_points[index] = access_points[index - 1U];
    access_points[insert_at] = *candidate;
}

bool at_module_w800_scan_access_points(at_module_t *module,
                                       at_w800_access_point_t *access_points,
                                       uint8_t capacity,
                                       uint8_t *count)
{
    char *line;
    char *next;

    if(module == NULL || module->session == NULL || access_points == NULL ||
       count == NULL || capacity == 0U || !w800_probe(module))
        return false;

    *count = 0U;
    memset(access_points, 0, (size_t)capacity * sizeof(*access_points));
    if(!at_session_cmd_capture_idle(module->session,
                                    "AT+WSCAN",
                                    "+OK",
                                    W800_SCAN_IDLE_MS,
                                    W800_SCAN_TIMEOUT_MS))
        return false;

    line = module->session->capture;
    while(line != NULL && *line != '\0')
    {
        at_w800_access_point_t candidate;

        next = strchr(line, '\n');
        if(next != NULL)
            *next++ = '\0';
        if(w800_parse_scan_line(line, &candidate))
            w800_insert_access_point(access_points, capacity, count, &candidate);
        line = next;
    }
    return true;
}


/* w800_parse_socket
 *
 * 从捕获的响应中解析 socket id。查找 "+OK=" 并取其后第一个数字字符作为 id。
 *
 * 返回:
 *  - >=0: 解析到的 socket id
 *  - -1: 解析失败
 *
 * 备注:
 *  - 解析逻辑简单，假设响应格式为 "+OK=<digit>"。若固件返回多位 id 或不同格式需增强解析。
 */
/** @brief Find a response prefix only at the beginning of a captured line. */
static const char *w800_find_capture_line_prefix(const char *capture,
                                                 const char *prefix)
{
    size_t prefix_length;
    const char *line;

    if(capture == NULL || prefix == NULL || prefix[0] == '\0')
        return NULL;
    prefix_length = strlen(prefix);
    line = capture;
    while(*line != '\0')
    {
        if(strncmp(line, prefix, prefix_length) == 0)
            return line + prefix_length;
        line = strchr(line, '\n');
        if(line == NULL)
            break;
        line++;
    }
    return NULL;
}

static int w800_parse_socket(const char *capture)
{
    char *end;
    long socket_id;
    const char *ok = w800_find_capture_line_prefix(capture, "+OK=");

    if(!ok)
        return -1;

    if(*ok < '0' || *ok > '9')
        return -1;

    socket_id = strtol(ok, &end, 10);
    if(end == ok || (*end != '\n' && *end != '\0') ||
       socket_id < 0L || socket_id > 255L)
        return -1;

    return (int)socket_id;
}

static int w800_parse_actual_size(const char *capture)
{
    char *end;
    long actual;
    const char *ok = w800_find_capture_line_prefix(capture, "+OK=");

    if(!ok)
        return -1;

    if(*ok < '0' || *ok > '9')
        return -1;

    actual = strtol(ok, &end, 10);
    if(end == ok || (*end != '\n' && *end != '\0') ||
       actual < 0L || actual > 65535L)
        return -1;

    return (int)actual;
}

/* w800_sleep_ms
 *
 * Sleeps through the session hook so the module driver does not depend on an
 * RTOS directly. W800 SKSND leaves a short transparent-data window after each
 * raw payload; a small gap keeps the next AT command out of that window on
 * firmware versions that need a few milliseconds to return to command mode.
 */
static void w800_sleep_ms(at_module_t *module, uint32_t ms)
{
    if(module && module->session && module->session->sleep_ms)
        module->session->sleep_ms(ms, module->session->sleep_arg);
}

/* w800_drain_command_tail
 *
 * Consumes delayed text responses after a transparent socket send window.
 * Some W800 firmware revisions emit or finish emitting "+OK" lines after the
 * raw SKSND payload has already been accepted. Leaving those lines in the UART
 * queue lets the next SKRCV binary reader mistake them for its own length
 * header, so the driver drains the AT parser briefly before returning.
 */
static void w800_drain_command_tail(at_module_t *module, uint32_t drain_ms)
{
    uint32_t start;

    if(!module || !module->session)
        return;

    start = at_session_now_ms(module->session);
    while((uint32_t)(at_session_now_ms(module->session) - start) < drain_ms)
    {
        at_session_poll_input(module->session);
        w800_sleep_ms(module, 1U);
    }
    at_client_clear_result(&module->session->client);
    at_session_clear_capture(module->session);
}

/* w800_open_socket
 *
 * 打开一个 TCP socket（通过 AT+SKCT）。
 *
 * 参数:
 *  - config: 指向 at_socket_config_t，包含 host、port、local_port 等
 *
 * 返回:
 *  - true: 打开成功并解析到 socket_id
 *  - false: 打开失败
 *
 * 备注:
 *  - 成功后将 module->socket_id 设置为解析到的 id。
 */
static bool w800_open_socket(at_module_t *module, const at_socket_config_t *config, int *socket_id)
{
    char cmd[128];
    int parsed;

    if(!config || !config->host || config->port == 0U || !socket_id)
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

    parsed = w800_parse_socket(at_session_capture(module->session));
    if(parsed < 0)
        return false;

    *socket_id = parsed;
    return true;
}

/* w800_send_socket
 *
 * 通过已打开的 socket 发送数据。流程:
 *  1. 发送 AT+SKSND=<id>,<len> 命令
 *  2. 等待短暂时间（模块准备接收原始数据）
 *  3. 直接发送原始数据（不加 CRLF）
 *  4. 等待 "+OK" 确认发送成功
 *
 * 返回:
 *  - true: 发送成功
 *  - false: 发送失败或 socket 未打开
 *
 * 备注:
 *  - 使用 at_client_send 发送命令并使用 at_session_send_raw 发送原始数据。
 *  - 发送后会清理 at_client 的结果状态。
 */
/* w800_send_socket
 *
 * Sends a complete TCP stream buffer through one W800 socket. The driver caps
 * every AT+SKSND request to a conservative window because some W800 firmware
 * revisions corrupt long transparent-data writes even when +OK=<actualsize>
 * appears to accept them. TCP preserves ordering, so MQTT/HTTP callers can pass
 * one complete protocol packet and let this driver split it safely.
 */
static bool w800_send_socket(at_module_t *module,
                             int socket_id,
                             const uint8_t *data,
                             uint16_t len,
                             uint16_t *actual_len)
{
    char cmd[64];
    uint16_t sent = 0U;

    if(!module || !module->session || socket_id < 0 || !data || len == 0U)
        return false;

    if(actual_len != NULL)
        *actual_len = 0U;

    while(sent < len)
    {
        const uint16_t remaining = (uint16_t)(len - sent);
        uint16_t request = remaining;
        int actual;

        if(request > W800_SOCKET_SEND_MAX)
            request = W800_SOCKET_SEND_MAX;

        at_session_clear_capture(module->session);
        (void)snprintf(cmd, sizeof(cmd),
                       "AT+SKSND=%d,%u",
                       socket_id,
                       (unsigned int)request);

        if(!at_session_cmd_expect(module->session,
                                  cmd,
                                  "+OK=",
                                  W800_CMD_TIMEOUT_MS,
                                  1U))
            goto fail;

        actual = w800_parse_actual_size(at_session_capture(module->session));
        if(actual <= 0 || actual > (int)request)
            goto fail;

        if(at_session_send_raw(module->session, &data[sent], (uint16_t)actual) != 0)
            goto fail;

        sent = (uint16_t)(sent + (uint16_t)actual);
        if(actual_len != NULL)
            *actual_len = sent;

        at_client_clear_result(&module->session->client);
        w800_sleep_ms(module, W800_SOCKET_SEND_GAP_MS);
        w800_drain_command_tail(module, W800_SOCKET_SEND_GAP_MS);
        if(at_session_recovery_required(module->session))
            goto fail;
    }

    at_client_clear_result(&module->session->client);
    return true;

fail:
    at_client_clear_result(&module->session->client);
    return false;
}

/* w800_recv_socket
 *
 * Reads payload bytes with AT+SKRCV=<socket>,<maxsize>. The AT session arms a
 * raw receive window after the +OK=<size> header, so bytes returned to callers
 * are network payload only and may contain CR/LF or other binary values.
 */
static bool w800_recv_socket(at_module_t *module,
                             int socket_id,
                             uint8_t *data,
                             uint16_t max_len,
                             uint16_t *actual_len)
{
    char cmd[64];
    uint16_t received = 0U;

    if(!module || !module->session || socket_id < 0 || !data || max_len == 0U || !actual_len)
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+SKRCV=%d,%u", socket_id, (unsigned int)max_len);
    if(!at_session_cmd_read_binary_ex(module->session,
                                      cmd,
                                      "+OK=",
                                      data,
                                      max_len,
                                      &received,
                                      AT_RAW_SEPARATOR_EMPTY_LINE,
                                      W800_CMD_TIMEOUT_MS,
                                      1U))
    {
        return false;
    }

    *actual_len = received;
    return true;
}

/* w800_close_socket
 *
 * 关闭当前 socket（如果存在），并将 socket_id 置为 -1。
 *
 * 返回:
 *  - true: 始终返回 true（即使 socket 未打开也视为成功）
 *
 * 备注:
 *  - 调用 AT+SKCLS=<id> 请求关闭，忽略返回值以保证清理继续进行。
 */
static bool w800_close_socket(at_module_t *module, int socket_id)
{
    char cmd[32];

    if(!module || !module->session || socket_id < 0)
        return true;

    (void)snprintf(cmd, sizeof(cmd), "AT+SKCLS=%d", socket_id);
    (void)at_session_cmd_expect(module->session, cmd, "+OK", 1000U, 1U);

    return true;
}

/* 导出驱动结构体
 *
 * 将 W800 驱动函数绑定到 at_module_driver_t，供上层通过 at_module 调用。
 */
const at_module_driver_t g_at_module_w800 =
{
    "W800",
    AT_MODULE_KIND_WIFI,
    w800_probe,
    w800_reset,
    NULL,
    w800_wifi_is_ready,
    w800_open_socket,
    w800_send_socket,
    w800_recv_socket,
    w800_close_socket
};
