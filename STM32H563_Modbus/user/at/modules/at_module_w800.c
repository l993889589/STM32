/*
 * at_module_w800.c
 *
 * W800 模块驱动实现（基于通用 at_module 接口）
 *
 * 概述:
 *  - 目的: 为 W800 WiFi 模块实现探测、复位、配置连接、扫描、打开/发送/关闭 socket 等功能，
 *    并将这些功能以 at_module_driver_t 的形式导出供上层使用。
 *  - 设计假设:
 *      * 使用 at_session / at_client 提供的同步命令发送与捕获机制。
 *      * 驱动通过 session 的日志回调输出调试信息（若已注册）。
 *      * 驱动在必要时调用底层 BSP（bsp_w800_hard_reset）进行硬件复位。
 *  - 超时与重试:
 *      * 常用命令超时定义为 W800_CMD_TIMEOUT_MS（3000 ms）
 *      * 加入 WiFi 的超时定义为 W800_JOIN_TIMEOUT_MS（45000 ms）
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

#include "drv_w800.h"
#include "at_command_plan.h"

/* 命令超时与加入网络超时（毫秒） */
#define W800_CMD_TIMEOUT_MS       3000U
#define W800_JOIN_TIMEOUT_MS      45000U
#define W800_SOCKET_SEND_MAX      256U
#define W800_SOCKET_SEND_GAP_MS   20U

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
    if(drv_w800_reset() != BSP_STATUS_OK)
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
    if(!at_session_cmd_expect(module->session, "AT+LKSTT", "+OK", W800_CMD_TIMEOUT_MS, 1U))
        return false;

    return strstr(at_session_capture(module->session), "+OK=1") != NULL;
}

/* w800_scan_wifi
 *
 * 执行 WiFi 扫描（AT+WSCAN），等待扫描完成并在捕获缓冲中查找指定 ssid。
 *
 * 参数:
 *  - ssid: 要查找的 SSID 字符串
 *
 * 返回:
 *  - true: 在扫描结果中找到指定 SSID
 *  - false: 未找到或扫描失败/超时
 *
 * 备注:
 *  - 发送扫描命令时使用 at_client_send 直接发送并等待 "+OK" 出现（长超时）。
 *  - 扫描完成后会短暂 sleep 并 poll 输入以收集结果。
 */
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

/* w800_wait_wifi_ready
 *
 * 在给定超时时间内轮询检查 WiFi 是否就绪（调用 w800_wifi_is_ready）。
 *
 * 参数:
 *  - timeout_ms: 最大等待时间（毫秒）
 *
 * 返回:
 *  - true: 在超时前检测到就绪
 *  - false: 超时未就绪
 */
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

/* w800_connect_network
 *
 * 配置并连接到指定的 WiFi 网络（STA 模式）。
 *
 * 参数:
 *  - config: 指向 at_wifi_config_t 的指针，包含 ssid 与 password 等
 *
 * 流程:
 *  1. probe 模块
 *  2. 设置工作模式、SSID、密码、NIP 等参数
 *  3. 保存配置并重启模块
 *  4. 扫描并尝试加入网络（AT+WJOIN），等待 DHCP 就绪
 *
 * 返回:
 *  - true: 成功连接并网络就绪
 *  - false: 任何一步失败
 *
 * 备注:
 *  - 使用多个 at_session_cmd_expect 调用，带重试次数。
 *  - 在保存配置后会短暂重启并再次 probe。
 */
static bool w800_connect_network(at_module_t *module, const void *config)
{
    const at_wifi_config_t *wifi = (const at_wifi_config_t *)config;
    char ssid_command[128];
    char key_command[128];
    at_command_step_t configure_steps[5];

    if(!wifi || !wifi->ssid || !wifi->password)
        return false;

    if(!w800_probe(module))
        return false;

    w800_log(module, "w800 state: config sta");
    (void)snprintf(ssid_command, sizeof(ssid_command), "AT+SSID=\"%s\"", wifi->ssid);
    (void)snprintf(key_command, sizeof(key_command), "AT+KEY=1,0,\"%s\"", wifi->password);

    configure_steps[0] = (at_command_step_t)
        {"station mode", "AT+WPRT=0", "+OK", W800_CMD_TIMEOUT_MS, 2U, AT_COMMAND_REQUIRED};
    configure_steps[1] = (at_command_step_t)
        {"ssid", ssid_command, "+OK", W800_CMD_TIMEOUT_MS, 2U, AT_COMMAND_REQUIRED};
    configure_steps[2] = (at_command_step_t)
        {"key", key_command, "+OK", W800_CMD_TIMEOUT_MS, 2U, AT_COMMAND_REQUIRED};
    configure_steps[3] = (at_command_step_t)
        {"dhcp", "AT+NIP=0", "+OK", W800_CMD_TIMEOUT_MS, 2U, AT_COMMAND_REQUIRED};
    configure_steps[4] = (at_command_step_t)
        {"save profile", "AT+PMTF", "+OK", W800_CMD_TIMEOUT_MS, 2U, AT_COMMAND_OPTIONAL};

    if(!at_command_plan_run(module->session,
                            configure_steps,
                            sizeof(configure_steps) / sizeof(configure_steps[0]),
                            NULL))
        return false;

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
static int w800_parse_socket(const char *capture)
{
    char *end;
    long socket_id;
    const char *ok = strstr(capture ? capture : "", "+OK=");

    if(!ok)
        return -1;

    ok += 4;
    if(*ok < '0' || *ok > '9')
        return -1;

    socket_id = strtol(ok, &end, 10);
    if(end == ok || socket_id < 0L || socket_id > 255L)
        return -1;

    return (int)socket_id;
}

static int w800_parse_actual_size(const char *capture)
{
    char *end;
    long actual;
    const char *ok = strstr(capture ? capture : "", "+OK=");

    if(!ok)
        return -1;

    ok += 4;
    if(*ok < '0' || *ok > '9')
        return -1;

    actual = strtol(ok, &end, 10);
    if(end == ok || actual < 0L || actual > 65535L)
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

        if(at_client_send(&module->session->client,
                          cmd,
                          at_session_now_ms(module->session),
                          W800_CMD_TIMEOUT_MS) != 0)
            goto fail;

        if(!at_session_wait_contains(module->session, "+OK=", W800_CMD_TIMEOUT_MS))
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
/** @brief Open a TCP server socket using the W800 SDK AT V1.1 SKCT format. */
bool at_module_w800_open_server(at_module_t *module,
                                uint16_t local_port,
                                uint32_t idle_timeout_s,
                                int *listener_socket)
{
    char command[96];
    int parsed;

    if((module == NULL) || (module->session == NULL) ||
       (local_port == 0U) || (listener_socket == NULL) ||
       (idle_timeout_s > 10000000U))
    {
        return false;
    }

    (void)snprintf(command, sizeof(command), "AT+SKCT=0,1,%lu,0,%u",
                   (unsigned long)idle_timeout_s, (unsigned int)local_port);
    at_session_clear_capture(module->session);
    if(!at_session_cmd_expect(module->session, command, "+OK=", 8000U, 1U))
    {
        return false;
    }
    parsed = w800_parse_socket(at_session_capture(module->session));
    if(parsed < 0)
    {
        return false;
    }
    *listener_socket = parsed;
    return true;
}

/** @brief Query SKSTT and select the first child socket in connected state. */
bool at_module_w800_accept_client(at_module_t *module,
                                  int listener_socket,
                                  int *client_socket)
{
    char command[32];
    const char *cursor;

    if((module == NULL) || (module->session == NULL) ||
       (listener_socket < 0) || (client_socket == NULL))
    {
        return false;
    }

    (void)snprintf(command, sizeof(command), "AT+SKSTT=%d", listener_socket);
    at_session_clear_capture(module->session);
    if(!at_session_cmd_expect(module->session, command, "+OK=", 1000U, 1U))
    {
        return false;
    }

    cursor = at_session_capture(module->session);
    while((cursor != NULL) && (*cursor != '\0'))
    {
        int socket_id;
        int state;
        const char *record = cursor;
        const char *next = strchr(cursor, '\n');

        if(strncmp(record, "+OK=", 4U) == 0)
        {
            record += 4;
        }
        if((sscanf(record, "%d,%d", &socket_id, &state) == 2) &&
           (socket_id != listener_socket) && (state == 2))
        {
            *client_socket = socket_id;
            return true;
        }
        cursor = next == NULL ? NULL : next + 1;
    }
    return false;
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
    w800_recv_socket,
    w800_close_socket
};
