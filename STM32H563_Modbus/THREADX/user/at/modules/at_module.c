/*
 * at_module.c
 *
 * AT 模块抽象层
 *
 * 概述:
 *  - 目的: 为具体的 AT 模块驱动（driver）和会话（session）提供轻量的封装，
 *    将模块驱动函数与会话绑定，并提供常用的模块操作代理函数（探测、复位、
 *    建立网络、打开/发送/关闭 socket 等）。
 *  - 设计假设:
 *      * at_module_t 由调用者分配并初始化。
 *      * driver 指针指向一个实现了 at_module_driver_t 接口的驱动结构体，
 *        驱动函数由具体模块实现并在 driver 中提供。
 *      * 本文件不做复杂错误处理，仅做空指针检查并调用驱动函数。
 *  - 主要职责:
 *      * 初始化模块结构体并保存 session、driver 与 driver_arg。
 *      * 提供一组简单的代理函数，按需调用 driver 中的对应函数并返回布尔结果。
 *
 * 使用示例:
 *  at_module_init(&mod, &session, &my_driver, my_driver_arg);
 *  if(at_module_probe(&mod)) 模块存在并可用 *
 *  at_module_connect_network(&mod, &net_cfg);
 *
 * 注意:
 *  - driver 中的函数指针可能为 NULL，代理函数会先检查再调用。
 *  - socket_id 初始为 -1，表示未打开 socket。
 */

#include "at_module.h"

#include <string.h>

/* at_module_init
 *
 * 初始化 at_module_t 结构体。
 *
 * 参数:
 *  - module: 要初始化的模块结构体指针
 *  - session: 关联的 at_session_t 指针（用于发送/接收 AT 命令）
 *  - driver: 指向模块驱动描述结构 at_module_driver_t 的指针
 *  - driver_arg: 传递给驱动的用户参数（驱动内部使用）
 *
 * 行为:
 *  - 将 module->session、module->driver、module->driver_arg 设定为传入值
 *  - 将 module->socket_id 设为 -1（表示未打开）
 *
 * 该函数不会对 driver 或 session 做深拷贝，仅保存指针。
 */
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

/* at_module_name
 *
 * 返回模块名称字符串。
 *
 * 行为:
 *  - 若 module、driver 或 driver->name 为 NULL，返回 "unknown"
 *  - 否则返回 driver->name
 *
 * 该函数用于日志或诊断输出。
 */
const char *at_module_name(const at_module_t *module)
{
    if(!module || !module->driver || !module->driver->name)
        return "unknown";

    return module->driver->name;
}

/* at_module_probe
 *
 * 探测模块是否存在/可用。
 *
 * 行为:
 *  - 若 module、driver 或 driver->probe 为 NULL，返回 false
 *  - 否则调用 driver->probe(module) 并返回其布尔结果
 *
 * 该函数将模块实例传给驱动的 probe 回调，驱动可使用 module->session 与 driver_arg。
 */
bool at_module_probe(at_module_t *module)
{
    return module && module->driver && module->driver->probe && module->driver->probe(module);
}

/* at_module_reset
 *
 * 复位模块（通过驱动实现）。
 *
 * 行为:
 *  - 若 driver->reset 存在则调用并返回其结果，否则返回 false
 */
bool at_module_reset(at_module_t *module)
{
    return module && module->driver && module->driver->reset && module->driver->reset(module);
}

/* at_module_connect_network
 *
 * 让模块连接到网络（例如注册蜂窝网络、建立 PDP/PDN 等）。
 *
 * 参数:
 *  - config: 指向网络配置的指针（驱动定义具体结构）
 *
 * 行为:
 *  - 若 driver->connect_network 存在则调用并返回其结果，否则返回 false
 */
bool at_module_connect_network(at_module_t *module, const void *config)
{
    return module && module->driver && module->driver->connect_network &&
           module->driver->connect_network(module, config);
}

/* at_module_is_network_ready
 *
 * 检查模块网络是否就绪（驱动实现）。
 *
 * 行为:
 *  - 若 driver->is_network_ready 存在则调用并返回其结果，否则返回 false
 */
bool at_module_is_network_ready(at_module_t *module)
{
    return module && module->driver && module->driver->is_network_ready &&
           module->driver->is_network_ready(module);
}

/* at_module_open_socket
 *
 * 打开一个网络 socket（驱动实现）。
 *
 * 参数:
 *  - config: 指向 at_socket_config_t 的配置指针（驱动定义具体字段）
 *
 * 行为:
 *  - 若 driver->open_socket 存在则调用并返回其结果，否则返回 false
 *
 * 注意:
 *  - 驱动通常会在成功时设置 module->socket_id 为有效值。
 */
bool at_module_open_socket_id(at_module_t *module, const at_socket_config_t *config, int *socket_id)
{
    return module && socket_id && module->driver && module->driver->open_socket &&
           module->driver->open_socket(module, config, socket_id);
}

/* at_module_send_socket
 *
 * 通过已打开的 socket 发送数据（驱动实现）。
 *
 * 参数:
 *  - data: 要发送的数据缓冲
 *  - len: 数据长度
 *
 * 行为:
 *  - 若 driver->send_socket 存在则调用并返回其结果，否则返回 false
 */
bool at_module_open_socket(at_module_t *module, const at_socket_config_t *config)
{
    int socket_id = -1;

    if(!at_module_open_socket_id(module, config, &socket_id))
        return false;

    module->socket_id = socket_id;
    return true;
}

bool at_module_send_socket_id(at_module_t *module, int socket_id, const uint8_t *data, uint16_t len, uint16_t *actual_len)
{
    return module && module->driver && module->driver->send_socket &&
           module->driver->send_socket(module, socket_id, data, len, actual_len);
}

bool at_module_send_socket(at_module_t *module, const uint8_t *data, uint16_t len)
{
    return module && module->socket_id >= 0 &&
           at_module_send_socket_id(module, module->socket_id, data, len, NULL);
}

bool at_module_recv_socket_id(at_module_t *module, int socket_id, uint8_t *data, uint16_t max_len, uint16_t *actual_len)
{
    return module && module->driver && module->driver->recv_socket &&
           module->driver->recv_socket(module, socket_id, data, max_len, actual_len);
}

bool at_module_close_socket_id(at_module_t *module, int socket_id)
{
    return module && module->driver && module->driver->close_socket &&
           module->driver->close_socket(module, socket_id);
}

/* at_module_close_socket
 *
 * 关闭当前 socket（驱动实现）。
 *
 * 行为:
 *  - 若 driver->close_socket 存在则调用并返回其结果，否则返回 false
 *  - 驱动在成功关闭后应将 module->socket_id 置为 -1（若需要）
 */
bool at_module_close_socket(at_module_t *module)
{
    if(!module || module->socket_id < 0)
        return false;

    if(!at_module_close_socket_id(module, module->socket_id))
        return false;

    module->socket_id = -1;
    return true;
}
