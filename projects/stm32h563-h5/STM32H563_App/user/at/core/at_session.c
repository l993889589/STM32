/*
 * at_session.c
 *
 * AT 会话封装层
 *
 * 概述:
 *  - 目的: 在 at_client 之上提供会话级别的便利函数，用于捕获并记录交互、
 *    提供阻塞等待/期望匹配、重试逻辑以及与平台时间/睡眠/轮询回调的集成。
 *  - 设计假设:
 *      * session 由调用者分配并初始化（无动态分配）。
 *      * session 的时间与睡眠函数由调用者注入（便于在不同平台或测试中替换）。
 *      * 会话捕获缓冲（capture）用于临时保存接收到的行，便于查找 token。
 *  - 主要职责:
 *      * 将 at_client 的行回调连接到会话捕获与日志。
 *      * 提供阻塞等待函数（wait_contains）和命令发送并期望响应的高层 API（cmd_expect）。
 *      * 提供原始发送接口 send_raw 以便直接发送字节。
 *
 * 使用示例:
 *  at_session_init(&sess, tx_cb, tx_arg, now_cb, time_arg, sleep_cb, sleep_arg);
 *  at_session_set_logger(&sess, log_cb, log_arg);
 *  at_session_cmd_expect(&sess, "AT+CMD", "OK", 2000, 3);
 *
 * 重要提示:
 *  - 会话捕获缓冲有固定大小，超出会被截断；捕获用于调试/匹配，不适合长期存储。
 *  - 本模块依赖 at_client 的回调机制，at_client 的并发语义仍需调用者保证。
 */

#include "at_session.h"

#include <stdio.h>
#include <string.h>

/* at_session_capture_append
 *
 * 将一行追加到 session->capture 捕获缓冲中，末尾以换行符分隔并保持 NUL 终止。
 * 若剩余空间不足则截断追加内容，确保不会越界。
 *
 * 参数:
 *  - session: 会话指针
 *  - line: 要追加的行（NUL 终止）
 *
 * 行为细节:
 *  - 计算可用空间 room = sizeof(capture) - 1 - capture_len（保留一个字节用于 NUL）。
 *  - 复制最多 room 字节的数据，更新 capture_len。
 *  - 若仍有空间，追加一个 '\n' 作为行分隔符。
 *  - 最终保证 capture 以 '\0' 结尾。
 */
static void at_session_capture_append(at_session_t *session, const char *line)
{
    uint32_t len;
    uint32_t room;

    if(!session || !line)
        return;

    len = (uint32_t)strlen(line);
    room = (uint32_t)(sizeof(session->capture) - 1U - session->capture_len);
    if(len > room)
        len = room;

    if(len != 0U)
    {
        memcpy(&session->capture[session->capture_len], line, len);
        session->capture_len = (uint16_t)(session->capture_len + len);
    }

    if(session->capture_len < (sizeof(session->capture) - 1U))
        session->capture[session->capture_len++] = '\n';

    session->capture[session->capture_len] = '\0';
}

/* at_session_line_callback
 *
 * 作为 at_client 的行回调注册。每当解析到一行时：
 *  - 将该行追加到会话捕获缓冲（用于后续匹配/调试）。
 *  - 若用户注册了日志回调，则将该行传递给日志回调。
 *
 * 参数:
 *  - line: 解析到的行
 *  - arg: 指向 at_session_t 的指针（通过 at_client_set_line_callback 传入）
 */
static void at_session_line_callback(const char *line, void *arg)
{
    at_session_t *session = (at_session_t *)arg;

    at_session_capture_append(session, line);

    if(session && session->log)
        session->log(line, session->log_arg);
}

/* at_session_now_ms
 *
 * 获取当前会话时间（毫秒）。如果 session->now_ms 回调存在则调用它，
 * 否则返回 0。
 *
 * 参数:
 *  - session: 会话指针
 *
 * 返回:
 *  - 当前时间（毫秒）或 0（如果未提供回调）
 */
uint32_t at_session_now_ms(const at_session_t *session)
{
    if(session && session->now_ms)
        return session->now_ms(session->time_arg);

    return 0U;
}

/* at_session_sleep
 *
 * 睡眠/延时封装。若提供了 sleep_ms 回调则调用它。
 *
 * 参数:
 *  - session: 会话指针
 *  - ms: 睡眠毫秒数
 */
static void at_session_sleep(at_session_t *session, uint32_t ms)
{
    if(session && session->sleep_ms)
        session->sleep_ms(ms, session->sleep_arg);
}

/* at_session_init
 *
 * 初始化 at_session_t 结构：
 *  - 清零结构体
 *  - 初始化内部的 at_client（传入 tx 回调）
 *  - 将 at_client 的行回调设置为 at_session_line_callback（用于捕获与日志）
 *  - 注入时间与睡眠回调
 *
 * 参数:
 *  - session: 会话实例指针
 *  - tx: 底层发送回调（传给 at_client）
 *  - tx_arg: 发送回调的用户参数
 *  - now_ms: 获取当前时间的回调（可为 NULL）
 *  - time_arg: 传给 now_ms 的用户参数
 *  - sleep_ms: 睡眠回调（可为 NULL）
 *  - sleep_arg: 传给 sleep_ms 的用户参数
 */
void at_session_init(at_session_t *session,
                     at_tx_cb_t tx,
                     void *tx_arg,
                     at_session_time_cb_t now_ms,
                     void *time_arg,
                     at_session_sleep_cb_t sleep_ms,
                     void *sleep_arg)
{
    if(!session)
        return;

    memset(session, 0, sizeof(*session));
    at_client_init(&session->client, tx, tx_arg);
    at_client_set_line_callback(&session->client, at_session_line_callback, session);
    session->now_ms = now_ms;
    session->time_arg = time_arg;
    session->sleep_ms = sleep_ms;
    session->sleep_arg = sleep_arg;
}

/* at_session_set_logger
 *
 * 设置会话日志回调。会话在接收到每行时会调用该回调（如果已设置）。
 *
 * 参数:
 *  - session: 会话指针
 *  - log: 日志回调函数（void (*)(const char *line, void *arg)）
 *  - arg: 传给日志回调的用户参数
 */
void at_session_set_logger(at_session_t *session, at_session_log_cb_t log, void *arg)
{
    if(!session)
        return;

    session->log = log;
    session->log_arg = arg;
}

/* at_session_set_poll_callback
 *
 * 设置一个轮询回调。该回调在会话等待/轮询期间被调用，用于驱动底层接收或其他事件处理。
 *
 * 参数:
 *  - session: 会话指针
 *  - poll: 轮询回调（void (*)(void *arg)）
 *  - arg: 传给 poll 的用户参数
 */
void at_session_set_poll_callback(at_session_t *session, at_session_poll_cb_t poll, void *arg)
{
    if(!session)
        return;

    session->poll = poll;
    session->poll_arg = arg;
}

/* at_session_clear_capture
 *
 * 清空会话捕获缓冲（capture_len 置 0，capture[0] 置 '\0'）。
 */
void at_session_clear_capture(at_session_t *session)
{
    if(!session)
        return;

    session->capture_len = 0U;
    session->capture[0] = '\0';
}

/* at_session_capture
 *
 * 返回捕获缓冲的只读指针（若 session 为 NULL 返回空字符串）。
 */
const char *at_session_capture(const at_session_t *session)
{
    return session ? session->capture : "";
}

/* at_session_input
 *
 * 将接收到的字节传递给内部的 at_client 进行解析。
 *
 * 参数:
 *  - session: 会话指针
 *  - data: 接收字节缓冲
 *  - len: 字节长度
 */
void at_session_input(at_session_t *session, const uint8_t *data, uint32_t len)
{
    if(!session || !data || len == 0U)
        return;

    at_client_input(&session->client, data, len);
}

/* at_session_poll_input
 *
 * 调用用户提供的 poll 回调（如果存在）。该回调通常用于驱动串口接收或处理底层事件。
 */
void at_session_poll_input(at_session_t *session)
{
    if(session && session->poll)
        session->poll(session->poll_arg);
}

/* at_session_wait_contains
 *
 * 阻塞等待直到 session 的 capture 或 at_client 的 response 中包含指定 token，
 * 或者超时（timeout_ms）到达。
 *
 * 参数:
 *  - session: 会话指针
 *  - token: 要查找的子串
 *  - timeout_ms: 超时时间（毫秒）
 *
 * 返回:
 *  - true: 在超时前找到 token
 *  - false: 超时或参数无效
 *
 * 实现细节:
 *  - 使用 at_session_now_ms 获取当前时间基准。
 *  - 在循环中调用 at_session_poll_input 以驱动输入处理。
 *  - 每次循环检查 capture 与 at_client_response 是否包含 token。
 *  - 每次循环调用 at_session_sleep(1) 以避免忙等（可由调用者实现为短延时）。
 */
bool at_session_wait_contains(at_session_t *session, const char *token, uint32_t timeout_ms)
{
    const uint32_t start = at_session_now_ms(session);

    if(!session || !token)
        return false;

    while((uint32_t)(at_session_now_ms(session) - start) < timeout_ms)
    {
        at_session_poll_input(session);

        if(strstr(session->capture, token) != NULL ||
           strstr(at_client_response(&session->client), token) != NULL)
        {
            return true;
        }

        at_session_sleep(session, 1U);
    }

    return false;
}

/* at_session_cmd_expect
 *
 * 发送命令并等待期望的响应字符串出现，支持超时与重试。
 *
 * 参数:
 *  - session: 会话指针
 *  - cmd: 要发送的 AT 命令字符串（不含 CRLF）
 *  - expect: 期望在 capture 或 response 中出现的子串
 *  - timeout_ms: 每次发送后的等待超时（毫秒）
 *  - retries: 最大重试次数（0 表示不发送）
 *
 * 返回:
 *  - true: 在某次尝试中找到了 expect
 *  - false: 所有尝试均失败或参数无效
 *
 * 行为细节:
 *  - 每次尝试前清空 capture。
 *  - 记录并通过日志回调输出发送的命令（如果已设置日志）。
 *  - 使用 at_client_send 发送命令并轮询 at_client_is_busy / at_client_poll。
 *  - 在等待期间定期调用 at_session_poll_input 并检查 capture/response 是否包含 expect。
 *  - 若超时或发送失败，则清理状态并在重试前等待 200ms。
 *  - 成功时调用 at_client_clear_result 并返回 true。
 *
 * 注意:
 *  - 该函数为阻塞实现，适用于测试或简单同步场景；在实时或严格事件驱动系统中应谨慎使用。
 */
bool at_session_cmd_expect(at_session_t *session,
                           const char *cmd,
                           const char *expect,
                           uint32_t timeout_ms,
                           uint8_t retries)
{
    char line[192];

    if(!session || !cmd || !expect)
        return false;

    for(uint8_t i = 0U; i < retries; i++)
    {
        at_session_clear_capture(session);

        if(session->log)
        {
            (void)snprintf(line, sizeof(line), "at tx: %s", cmd);
            session->log(line, session->log_arg);
        }

        if(at_client_send(&session->client, cmd, at_session_now_ms(session), timeout_ms) == 0)
        {
            while(at_client_is_busy(&session->client))
            {
                at_session_poll_input(session);

                if(strstr(session->capture, expect) != NULL ||
                   strstr(at_client_response(&session->client), expect) != NULL)
                {
                    at_client_clear_result(&session->client);
                    return true;
                }

                if(at_client_poll(&session->client, at_session_now_ms(session)) == AT_RESULT_TIMEOUT)
                    break;

                at_session_sleep(session, 1U);
            }

            if(strstr(session->capture, expect) != NULL ||
               strstr(at_client_response(&session->client), expect) != NULL)
            {
                at_client_clear_result(&session->client);
                return true;
            }
        }

        at_client_clear_result(&session->client);

        if(session->log)
            session->log("at warn: command timeout", session->log_arg);

        at_session_sleep(session, 200U);
    }

    return false;
}

/* at_session_send_raw
 *
 * 直接通过底层 tx 回调发送原始字节（不经过 at_client 的 CRLF 封装）。
 *
 * 参数:
 *  - session: 会话指针
 *  - data: 要发送的字节缓冲
 *  - len: 字节长度
 *
 * 返回:
 *  - tx 回调的返回值（通常 0 表示成功），或 -1 表示参数错误
 */
int at_session_send_raw(at_session_t *session, const uint8_t *data, uint16_t len)
{
    if(!session || !session->client.tx || !data || len == 0U)
        return -1;

    return session->client.tx(data, len, session->client.tx_arg);
}
