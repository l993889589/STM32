/*
 * at_module_ec20.c
 *
 * EC20 模块驱动实现（基于通用 at_module 接口）
 *
 * 概述:
 *  - 目的: 为 Quectel EC20 蜂窝模块实现探测、复位、配置网络、打开/发送/关闭 socket 等功能，
 *    并以 at_module_driver_t 的形式导出供上层调用。
 *  - 设计假设:
 *      * 使用 at_session / at_client 提供的同步命令发送与捕获机制。
 *      * 驱动通过 session 的日志回调输出调试信息（若已注册）。
 *      * 驱动在必要时会等待（sleep）以给模块时间完成重启或网络注册。
 *  - 超时与重试:
 *      * 常用命令超时定义为 EC20_CMD_TIMEOUT_MS（3000 ms）
 *      * 网络连接超时定义为 EC20_NET_TIMEOUT_MS（60000 ms）
 *      * socket 操作超时定义为 EC20_SOCKET_TIMEOUT_MS（30000 ms）
 *
 * 注意:
 *  - 本驱动对 AT 响应的解析以字符串匹配为主（例如查找 "OK"、"+QIOPEN" 等），
 *    在面对不同固件版本或非标准响应时可能需要增强解析逻辑。
 *  - 本文件中多数函数在失败时返回 false，上层可据此决定重试或回退策略。
 */

#include "at_module_ec20.h"

#include <stdio.h>
#include <string.h>

/* 超时定义（毫秒） */
#define EC20_CMD_TIMEOUT_MS       3000U
#define EC20_NET_TIMEOUT_MS       60000U
#define EC20_SOCKET_TIMEOUT_MS    30000U

/* ec20_log
 *
 * 将驱动内部日志通过 session 的日志回调转发（如果已注册）。
 * 便于统一日志输出，不直接使用 printf。
 *
 * 参数:
 *  - module: 模块实例指针
 *  - line: 要记录的日志行
 */
static void ec20_log(at_module_t *module, const char *line)
{
    if(module && module->session && module->session->log && line)
        module->session->log(line, module->session->log_arg);
}

/* ec20_probe
 *
 * 探测 EC20 模块是否响应 AT 命令。
 *
 * 返回:
 *  - true: 模块响应且兼容 AT
 *  - false: 未响应
 *
 * 备注:
 *  - 使用 at_session_cmd_expect 进行阻塞式发送与期望匹配，带重试。
 */
static bool ec20_probe(at_module_t *module)
{
    ec20_log(module, "ec20 state: probe");
    return at_session_cmd_expect(module->session, "AT", "OK", 1000U, 5U);
}

/** @brief Recover an ambiguous EC20 session through a caller-owned hard reset. */
static bool ec20_recover_with_hard_reset(at_module_t *module)
{
    const at_module_ec20_platform_t *platform;

    if(module == NULL || module->session == NULL)
        return false;
    platform = (const at_module_ec20_platform_t *)module->driver_arg;
    if(platform == NULL || platform->hard_reset == NULL ||
       !platform->hard_reset(platform->argument))
    {
        return false;
    }
    if(module->session->sleep_ms != NULL && platform->ready_delay_ms != 0U)
    {
        module->session->sleep_ms(platform->ready_delay_ms,
                                  module->session->sleep_arg);
    }
    return at_session_recover_after_transport_reset(module->session);
}

/* ec20_reset
 *
 * 通过发送复位命令（AT+CFUN=1,1）重启模块，并在必要时等待模块重启完成。
 *
 * 返回:
 *  - true: 复位命令发送成功并在等待后 probe 成功
 *  - false: 复位或后续 probe 失败
 *
 * 备注:
 *  - 发送复位后会调用 sleep_ms 等待 5 秒以便模块重启。
 *  - 成功后将 module->socket_id 置为 -1。
 */
static bool ec20_reset(at_module_t *module)
{
    ec20_log(module, "ec20 state: reset");

    if(at_session_recovery_required(module->session))
    {
        if(!ec20_recover_with_hard_reset(module))
            return false;
    }
    else
    {
        if(!at_session_cmd_expect(module->session,
                                  "AT+CFUN=1,1",
                                  "OK",
                                  EC20_CMD_TIMEOUT_MS,
                                  1U))
        {
            if(!ec20_recover_with_hard_reset(module))
                return false;
        }
        else
        {
            if(module->session->sleep_ms)
                module->session->sleep_ms(5000U, module->session->sleep_arg);
            if(!at_session_recover_after_transport_reset(module->session))
                return false;
        }
    }

    module->socket_id = -1;
    return ec20_probe(module);
}

/* ec20_is_network_ready
 *
 * 检查模块是否已附着到网络（CGATT=1）。
 *
 * 返回:
 *  - true: 网络已附着
 *  - false: 未附着或命令失败
 *
 * 备注:
 *  - 使用 at_session_cmd_expect 检查 "+CGATT: 1" 出现。
 */
static bool ec20_is_network_ready(at_module_t *module)
{
    if(!at_session_cmd_expect(module->session, "AT+CGATT?", "+CGATT: 1", EC20_CMD_TIMEOUT_MS, 1U))
        return false;

    return true;
}

/* ec20_wait_network
 *
 * 在给定超时时间内轮询检查网络是否就绪（调用 ec20_is_network_ready）。
 *
 * 参数:
 *  - timeout_ms: 最大等待时间（毫秒）
 *
 * 返回:
 *  - true: 在超时前检测到网络就绪
 *  - false: 超时未就绪
 */
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

/* ec20_connect_network
 *
 * 配置并激活数据连接（PDP/PDN），使用 QICSGP/QIACT 等 AT 命令。
 *
 * 参数:
 *  - config: 指向 at_cellular_config_t 的指针，至少包含 apn 字段
 *
 * 返回:
 *  - true: 成功激活数据连接并网络就绪
 *  - false: 任何一步失败
 *
 * 流程:
 *  1. probe 模块
 *  2. 关闭回显（ATE0）
 *  3. 检查 SIM 就绪（AT+CPIN? -> READY）
 *  4. 配置 PDP 参数（AT+QICSGP）
 *  5. 激活 PDP（AT+QIACT=1）
 *  6. 等待网络就绪（ec20_wait_network）
 */
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

/* ec20_open_socket
 *
 * 打开一个 TCP socket（使用 QIOPEN）。
 *
 * 参数:
 *  - config: 指向 at_socket_config_t，包含 host、port 等
 *
 * 返回:
 *  - true: 打开成功（QIOPEN 返回 OK 且随后出现 +QIOPEN: 0,0）
 *  - false: 打开失败
 *
 * 备注:
 *  - 这里将 module->socket_id 暂设为 0（示例），实际驱动可能需要解析返回的 socket id。
 */
static bool ec20_open_socket(at_module_t *module, const at_socket_config_t *config, int *socket_id)
{
    char cmd[160];
    int id = 0;

    if(!config || !config->host || config->port == 0U || !socket_id)
        return false;

    (void)snprintf(cmd, sizeof(cmd),
                   "AT+QIOPEN=1,%d,\"TCP\",\"%s\",%u,0,0",
                   id,
                   config->host,
                   (unsigned int)config->port);

    if(!at_session_cmd_expect(module->session, cmd, "OK", EC20_SOCKET_TIMEOUT_MS, 1U))
        return false;

    if(!at_session_wait_contains(module->session, "+QIOPEN: 0,0", EC20_SOCKET_TIMEOUT_MS))
        return false;

    *socket_id = id;
    return true;
}

/* ec20_send_socket
 *
 * 通过已打开的 socket 发送数据（使用 QISEND）。
 *
 * 流程:
 *  1. 发送 AT+QISEND=<id>,<len> 并等待 '>' 提示
 *  2. 发送原始数据（不加 CRLF）
 *  3. 等待 "SEND OK" 确认
 *
 * 返回:
 *  - true: 发送成功
 *  - false: 发送失败或 socket 未打开
 */
static bool ec20_send_socket(at_module_t *module,
                             int socket_id,
                             const uint8_t *data,
                             uint16_t len,
                             uint16_t *actual_len)
{
    char cmd[64];

    if(!module || !module->session || socket_id < 0 || !data || len == 0U)
        return false;

    (void)snprintf(cmd, sizeof(cmd), "AT+QISEND=%d,%u", socket_id, (unsigned int)len);
    if(!at_session_cmd_expect(module->session, cmd, ">", EC20_CMD_TIMEOUT_MS, 1U))
        return false;

    if(at_session_send_raw(module->session, data, len) != 0)
        return false;

    if(actual_len != NULL)
        *actual_len = len;
    return at_session_wait_contains(module->session, "SEND OK", EC20_SOCKET_TIMEOUT_MS);
}

static bool ec20_recv_socket(at_module_t *module,
                             int socket_id,
                             uint8_t *data,
                             uint16_t max_len,
                             uint16_t *actual_len)
{
    (void)module;
    (void)socket_id;
    (void)data;
    (void)max_len;
    if(actual_len != NULL)
        *actual_len = 0U;
    return false;
}

/* ec20_close_socket
 *
 * 关闭当前 socket（如果存在），并将 socket_id 置为 -1。
 *
 * 返回:
 *  - true: 始终返回 true（即使 socket 未打开也视为成功）
 *
 * 备注:
 *  - 调用 AT+QICLOSE=<id> 请求关闭，忽略返回值以保证清理继续进行。
 */
static bool ec20_close_socket(at_module_t *module, int socket_id)
{
    char cmd[32];

    if(!module || !module->session || socket_id < 0)
        return true;

    (void)snprintf(cmd, sizeof(cmd), "AT+QICLOSE=%d", socket_id);
    (void)at_session_cmd_expect(module->session, cmd, "OK", EC20_CMD_TIMEOUT_MS, 1U);

    return true;
}

/* 导出驱动结构体
 *
 * 将 EC20 驱动函数绑定到 at_module_driver_t，供上层通过 at_module 调用。
 */
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
    ec20_recv_socket,
    ec20_close_socket
};
