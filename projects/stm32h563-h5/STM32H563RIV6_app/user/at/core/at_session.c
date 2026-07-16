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
#include <stdlib.h>
#include <string.h>

#define AT_SESSION_RESYNC_QUIET_MS 200U
#define AT_SESSION_RESYNC_MAX_MS   1000U
#define AT_SESSION_RESYNC_MAX_POLLS (AT_SESSION_RESYNC_MAX_MS + 1U)
#define AT_SESSION_WAIT_POLLS_PER_MS 16U
#define AT_SESSION_WAIT_POLL_MARGIN  64U

static void at_session_raw_clear(at_session_t *session);
static void at_session_sync_command_parser(at_session_t *session);

/** @brief Bound blocking loops even if a supposedly monotonic clock stalls. */
static uint32_t at_session_wait_poll_budget(uint32_t timeout_ms)
{
    if(timeout_ms > ((UINT32_MAX - AT_SESSION_WAIT_POLL_MARGIN) /
                     AT_SESSION_WAIT_POLLS_PER_MS))
        return UINT32_MAX;
    return timeout_ms * AT_SESSION_WAIT_POLLS_PER_MS +
           AT_SESSION_WAIT_POLL_MARGIN;
}

/** @brief Prevent reuse after an ambiguous transaction failure. */
static void at_session_require_transport_reset(at_session_t *session)
{
    if(session != NULL)
    {
        session->desynchronized = 1U;
        session->recovery_required = 1U;
    }
}

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

/** @brief Match a token only at the start of a captured response line. */
static bool at_session_capture_has_line_prefix(const at_session_t *session,
                                               const char *prefix)
{
    const char *line;
    size_t prefix_length;

    if(session == NULL || prefix == NULL || prefix[0] == '\0')
        return false;
    prefix_length = strlen(prefix);
    line = session->capture;
    while(*line != '\0')
    {
        if(strncmp(line, prefix, prefix_length) == 0)
            return true;
        line = strchr(line, '\n');
        if(line == NULL)
            return false;
        line++;
    }
    return false;
}

/* Parses the decimal length immediately following a module-specific prefix.
 * Rejecting missing digits and trailing text prevents malformed or stale lines
 * from silently becoming a zero-length successful binary read.
 */
static bool at_session_parse_binary_length(const char *line,
                                           const char *prefix,
                                           uint32_t *length)
{
    const char *text;
    char *end;
    unsigned long parsed;

    if(!line || !prefix || !length)
        return false;

    text = line + strlen(prefix);
    if(*text < '0' || *text > '9')
        return false;

    parsed = strtoul(text, &end, 10);
    while(*end == ' ' || *end == '\t')
        end++;
    if(*end != '\0' || parsed > 65535UL)
        return false;

    *length = (uint32_t)parsed;
    return true;
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

    if(session &&
       session->raw_waiting != 0U &&
       session->raw_header_prefix != NULL &&
       strncmp(line, session->raw_header_prefix, strlen(session->raw_header_prefix)) == 0)
    {
        uint32_t expected;

        session->raw_waiting = 0U;
        if(!at_session_parse_binary_length(line, session->raw_header_prefix, &expected))
        {
            session->raw_error = 1U;
            session->binary_diag.header_errors++;
            return;
        }
        if(expected > session->raw_capacity)
        {
            session->raw_error = 1U;
            session->binary_diag.capacity_errors++;
            return;
        }

        session->raw_expected = (uint16_t)expected;
        if(expected == 0U)
        {
            session->raw_done = 1U;
            return;
        }

        if(!at_client_raw_begin_ex(&session->client,
                                   session->raw_buffer,
                                   (uint16_t)expected,
                                   session->raw_separator))
        {
            session->raw_error = 1U;
            session->binary_diag.raw_begin_errors++;
        }
    }
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

/** @brief Drain a failed transaction until the transport remains quiet. */
static bool at_session_resynchronize(at_session_t *session)
{
    uint32_t last_epoch;
    uint32_t polls = 0U;
    uint32_t quiet_start;
    uint32_t start;
    bool quiet_observed = false;

    if(session == NULL)
        return false;
    if(session->desynchronized == 0U)
        return true;

    at_client_clear_result(&session->client);
    at_session_raw_clear(session);
    at_session_clear_capture(session);
    start = at_session_now_ms(session);
    quiet_start = start;
    last_epoch = session->rx_epoch;

    while((uint32_t)(at_session_now_ms(session) - start) <
          AT_SESSION_RESYNC_MAX_MS && polls++ < AT_SESSION_RESYNC_MAX_POLLS)
    {
        uint32_t now;

        at_session_poll_input(session);
        now = at_session_now_ms(session);
        if(session->rx_epoch != last_epoch)
        {
            last_epoch = session->rx_epoch;
            quiet_start = now;
        }
        if((uint32_t)(now - quiet_start) >= AT_SESSION_RESYNC_QUIET_MS)
        {
            quiet_observed = true;
            break;
        }
        at_session_sleep(session, 1U);
    }

    at_session_raw_clear(session);
    at_client_clear_result(&session->client);
    at_session_sync_command_parser(session);
    session->client.line_discarding = 0U;
    at_session_clear_capture(session);
    if(!quiet_observed)
    {
        session->command_diag.resync_failures++;
        at_session_require_transport_reset(session);
        return false;
    }
    session->desynchronized = 0U;
    session->command_diag.resynchronizations++;
    return true;
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

    session->rx_epoch += len;
    at_client_input(&session->client, data, len);
}

/** @brief Fail closed after UART/LDC reports a discontinuous receive stream. */
void at_session_transport_error(at_session_t *session)
{
    if(session == NULL)
        return;

    if(session->raw_waiting != 0U || session->client.raw_active != 0U)
        session->binary_diag.transport_errors++;
    session->raw_waiting = 0U;
    session->raw_done = 0U;
    session->raw_error = 1U;
    session->transport_error_epoch++;
    at_session_require_transport_reset(session);
    session->command_diag.transport_errors++;
    at_client_transport_error(&session->client);
}

/** @brief Reset parser state only after the peer/transport was externally reset. */
bool at_session_recover_after_transport_reset(at_session_t *session)
{
    uint32_t error_epoch;

    if(session == NULL || session->now_ms == NULL)
        return false;

    error_epoch = session->transport_error_epoch;
    at_client_transport_error(&session->client);
    at_session_raw_clear(session);
    session->desynchronized = 1U;
    if(!at_session_resynchronize(session))
        return false;
    if(session->transport_error_epoch != error_epoch)
    {
        at_session_require_transport_reset(session);
        return false;
    }
    session->recovery_required = 0U;
    session->command_diag.transport_resets++;
    return true;
}

/** @brief Report whether new commands are blocked pending an external reset. */
bool at_session_recovery_required(const at_session_t *session)
{
    return session != NULL && session->recovery_required != 0U;
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
    at_result_t result;
    uint32_t polls = 0U;
    const uint32_t poll_budget = at_session_wait_poll_budget(timeout_ms);

    if(!session || !token || token[0] == '\0')
        return false;
    if(session->recovery_required != 0U)
    {
        session->command_diag.recovery_blocks++;
        return false;
    }

    if(!at_session_resynchronize(session))
        return false;
    if(at_client_is_busy(&session->client))
    {
        at_client_set_success_prefix(&session->client, token);
    }
    else if(at_client_begin_wait(&session->client,
                                 token,
                                 at_session_now_ms(session),
                                 timeout_ms) != 0)
    {
        return false;
    }

    while(at_client_is_busy(&session->client) && polls++ < poll_budget)
    {
        at_session_poll_input(session);
        (void)at_client_poll(&session->client, at_session_now_ms(session));
        at_session_sleep(session, 1U);
    }

    if(at_client_is_busy(&session->client))
    {
        session->command_diag.poll_limit_exits++;
        at_session_require_transport_reset(session);
    }

    result = at_client_poll(&session->client, at_session_now_ms(session));
    if(result == AT_RESULT_OK)
    {
        at_client_clear_result(&session->client);
        return true;
    }
    if(result == AT_RESULT_TIMEOUT || result == AT_RESULT_OVERFLOW ||
       result == AT_RESULT_TRANSPORT_ERROR)
    {
        at_session_require_transport_reset(session);
    }
    at_client_clear_result(&session->client);
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
    at_result_t result;

    if(!session || !cmd || !expect || expect[0] == '\0')
        return false;
    if(session->recovery_required != 0U)
    {
        session->command_diag.recovery_blocks++;
        return false;
    }

    for(uint8_t i = 0U; i < retries; i++)
    {
        uint32_t polls = 0U;
        const uint32_t poll_budget = at_session_wait_poll_budget(timeout_ms);

        if(!at_session_resynchronize(session))
            break;
        at_session_clear_capture(session);
        at_client_set_success_prefix(&session->client, expect);
        session->command_diag.attempts++;

        if(session->log)
        {
            (void)snprintf(line, sizeof(line), "at tx: %s", cmd);
            session->log(line, session->log_arg);
        }

        if(at_client_send(&session->client,
                          cmd,
                          at_session_now_ms(session),
                          timeout_ms) == 0)
        {
            while(at_client_is_busy(&session->client) && polls++ < poll_budget)
            {
                at_session_poll_input(session);
                (void)at_client_poll(&session->client,
                                     at_session_now_ms(session));
                at_session_sleep(session, 1U);
            }

            if(at_client_is_busy(&session->client))
            {
                session->command_diag.poll_limit_exits++;
                at_session_require_transport_reset(session);
            }

            result = at_client_poll(&session->client,
                                    at_session_now_ms(session));
            if(result == AT_RESULT_OK)
            {
                session->command_diag.successes++;
                at_client_clear_result(&session->client);
                return true;
            }
            if(result == AT_RESULT_ERROR)
                session->command_diag.module_errors++;
            else if(result == AT_RESULT_TIMEOUT)
                session->command_diag.timeouts++;
            else if(result == AT_RESULT_OVERFLOW)
                session->command_diag.overflows++;
            if(result == AT_RESULT_TIMEOUT || result == AT_RESULT_OVERFLOW ||
               result == AT_RESULT_TRANSPORT_ERROR)
            {
                at_session_require_transport_reset(session);
            }
        }
        else
        {
            session->command_diag.tx_errors++;
            at_session_require_transport_reset(session);
        }

        at_client_clear_result(&session->client);

        if(session->log)
            session->log("at warn: command failed", session->log_arg);
        if(session->recovery_required != 0U)
            break;
    }

    return false;
}

bool at_session_cmd_capture_idle(at_session_t *session,
                                 const char *cmd,
                                 const char *start_token,
                                 uint32_t idle_ms,
                                 uint32_t timeout_ms)
{
    uint32_t start;
    uint32_t last_input;
    uint32_t polls = 0U;
    uint32_t poll_budget;
    at_result_t result;
    uint16_t last_length = 0U;
    uint8_t response_started = 0U;

    if(session == NULL || cmd == NULL || start_token == NULL || idle_ms == 0U)
        return false;
    if(session->recovery_required != 0U)
    {
        session->command_diag.recovery_blocks++;
        return false;
    }

    if(!at_session_resynchronize(session))
        return false;
    at_session_clear_capture(session);
    start = at_session_now_ms(session);
    last_input = start;
    poll_budget = at_session_wait_poll_budget(timeout_ms);
    if(at_client_send(&session->client, cmd, start, timeout_ms) != 0)
    {
        session->command_diag.tx_errors++;
        at_session_require_transport_reset(session);
        return false;
    }

    while((uint32_t)(at_session_now_ms(session) - start) < timeout_ms &&
          polls++ < poll_budget)
    {
        uint32_t now;
        at_session_poll_input(session);
        now = at_session_now_ms(session);
        result = at_client_poll(&session->client, now);
        if(session->recovery_required != 0U || result == AT_RESULT_ERROR ||
           result == AT_RESULT_OVERFLOW || result == AT_RESULT_TIMEOUT ||
           result == AT_RESULT_TRANSPORT_ERROR)
            break;
        if(session->capture_len != last_length)
        {
            last_length = session->capture_len;
            last_input = now;
        }
        if(at_session_capture_has_line_prefix(session, start_token))
            response_started = 1U;
        if(response_started != 0U &&
           (uint32_t)(now - last_input) >= idle_ms)
        {
            at_client_clear_result(&session->client);
            return true;
        }

        at_session_sleep(session, 1U);
    }

    if(polls >= poll_budget &&
       (uint32_t)(at_session_now_ms(session) - start) < timeout_ms)
    {
        session->command_diag.poll_limit_exits++;
        at_session_require_transport_reset(session);
    }

    result = at_client_poll(&session->client, at_session_now_ms(session));
    if(result == AT_RESULT_TIMEOUT || result == AT_RESULT_OVERFLOW ||
       result == AT_RESULT_TRANSPORT_ERROR)
    {
        at_session_require_transport_reset(session);
    }
    at_client_clear_result(&session->client);
    return false;
}

static void at_session_raw_clear(at_session_t *session)
{
    if(!session)
        return;

    session->raw_header_prefix = NULL;
    session->raw_buffer = NULL;
    session->raw_capacity = 0U;
    session->raw_expected = 0U;
    session->raw_separator = AT_RAW_SEPARATOR_NONE;
    session->raw_waiting = 0U;
    session->raw_done = 0U;
    session->raw_error = 0U;
    at_client_raw_cancel(&session->client);
}

/* at_session_sync_command_parser
 *
 * Resets parser state that is local to one AT command before starting a
 * length-prefixed binary read. This does not discard bytes already buffered in
 * the UART/LDC layer; it only prevents an unfinished text line from a previous
 * command from being prepended to the next +OK=<len> header.
 */
static void at_session_sync_command_parser(at_session_t *session)
{
    if(!session)
        return;

    session->client.line_len = 0U;
    session->client.line_discarding = 0U;
    session->client.response_len = 0U;
    session->client.response[0] = '\0';
}

/* at_session_drain_stale_input
 *
 * Runs the platform input pump briefly before a binary read command. Socket
 * transmit commands such as W800 SKSND can leave their trailing text response
 * in the UART/LDC software queue even after the caller has seen "+OK=". Those
 * old lines must be consumed before raw_waiting is armed, otherwise a stale
 * "+OK=<n>" can be mistaken for the next SKRCV length header.
 */
static void at_session_drain_stale_input(at_session_t *session, uint32_t drain_ms)
{
    const uint32_t start = at_session_now_ms(session);
    uint32_t polls = 0U;

    if(!session)
        return;

    while((uint32_t)(at_session_now_ms(session) - start) < drain_ms &&
          polls++ <= drain_ms)
    {
        at_session_poll_input(session);
        at_session_sleep(session, 1U);
    }
}

/* at_session_cmd_read_binary
 *
 * Sends a command whose response contains a text header followed by an exact
 * binary payload. The header must start with header_prefix and the decimal
 * length must immediately follow the prefix, for example "+OK=512".
 *
 * The raw payload is copied into buffer and never enters the CR/LF line parser.
 * Use this for modem socket read commands such as W800 AT+SKRCV. It is not a
 * protocol parser; module drivers remain responsible for command syntax and for
 * validating whether the received byte count is acceptable for their command.
 */
bool at_session_cmd_read_binary(at_session_t *session,
                                const char *cmd,
                                const char *header_prefix,
                                uint8_t *buffer,
                                uint16_t max_len,
                                uint16_t *out_len,
                                uint32_t timeout_ms,
                                uint8_t retries)
{
    return at_session_cmd_read_binary_ex(session,
                                         cmd,
                                         header_prefix,
                                         buffer,
                                         max_len,
                                         out_len,
                                         AT_RAW_SEPARATOR_NONE,
                                         timeout_ms,
                                         retries);
}

bool at_session_cmd_read_binary_ex(at_session_t *session,
                                   const char *cmd,
                                   const char *header_prefix,
                                   uint8_t *buffer,
                                   uint16_t max_len,
                                   uint16_t *out_len,
                                   at_raw_separator_t separator,
                                   uint32_t timeout_ms,
                                   uint8_t retries)
{
    if(!session || !cmd || !header_prefix || !buffer || max_len == 0U || !out_len ||
       (separator != AT_RAW_SEPARATOR_NONE && separator != AT_RAW_SEPARATOR_EMPTY_LINE))
        return false;
    if(session->recovery_required != 0U)
    {
        session->command_diag.recovery_blocks++;
        return false;
    }

    for(uint8_t i = 0U; i < retries; i++)
    {
        uint32_t start;
        uint32_t polls = 0U;
        uint32_t poll_budget;
        at_result_t result = AT_RESULT_NONE;

        session->binary_diag.attempts++;

        if(!at_session_resynchronize(session))
            break;
        at_session_clear_capture(session);
        at_session_raw_clear(session);
        at_session_sync_command_parser(session);
        at_session_drain_stale_input(session, 3U);
        if(session->recovery_required != 0U)
        {
            at_session_raw_clear(session);
            break;
        }
        at_session_clear_capture(session);
        at_session_sync_command_parser(session);
        session->raw_header_prefix = header_prefix;
        session->raw_buffer = buffer;
        session->raw_capacity = max_len;
        session->raw_separator = separator;
        session->raw_waiting = 1U;
        *out_len = 0U;
        start = at_session_now_ms(session);
        poll_budget = at_session_wait_poll_budget(timeout_ms);

        if(at_client_send(&session->client, cmd, start, timeout_ms) != 0)
        {
            session->command_diag.tx_errors++;
            at_session_require_transport_reset(session);
            at_session_raw_clear(session);
            break;
        }

        while((uint32_t)(at_session_now_ms(session) - start) < timeout_ms &&
              polls++ < poll_budget)
        {
            at_session_poll_input(session);
            if(session->raw_error != 0U)
                break;

            if(session->raw_done != 0U || at_client_raw_is_done(&session->client))
            {
                *out_len = session->raw_expected;
                session->binary_diag.successes++;
                at_client_clear_result(&session->client);
                at_session_raw_clear(session);
                return true;
            }

            result = at_client_poll(&session->client,
                                    at_session_now_ms(session));
            if(result == AT_RESULT_ERROR || result == AT_RESULT_OVERFLOW ||
               result == AT_RESULT_TRANSPORT_ERROR)
                break;

            at_session_sleep(session, 1U);
        }

        if(polls >= poll_budget &&
           (uint32_t)(at_session_now_ms(session) - start) < timeout_ms)
        {
            session->command_diag.poll_limit_exits++;
            at_session_require_transport_reset(session);
        }

        if(session->raw_error == 0U)
            session->binary_diag.timeouts++;
        if(session->raw_error != 0U || session->raw_waiting != 0U ||
           at_client_is_busy(&session->client) ||
           result == AT_RESULT_TIMEOUT || result == AT_RESULT_OVERFLOW ||
           result == AT_RESULT_TRANSPORT_ERROR ||
           at_client_raw_is_active(&session->client))
        {
            at_session_require_transport_reset(session);
        }

        at_client_clear_result(&session->client);
        at_session_raw_clear(session);
        if(session->recovery_required != 0U)
            break;
    }

    return false;
}

void at_session_binary_diag(const at_session_t *session, at_session_binary_diag_t *diag)
{
    if(diag != NULL)
    {
        if(session != NULL)
            *diag = session->binary_diag;
        else
            memset(diag, 0, sizeof(*diag));
    }
}

void at_session_binary_diag_reset(at_session_t *session)
{
    if(session != NULL)
        memset(&session->binary_diag, 0, sizeof(session->binary_diag));
}

/** @brief Copy line-oriented AT command diagnostics. */
void at_session_command_diag(const at_session_t *session,
                             at_session_command_diag_t *diag)
{
    if(diag != NULL)
    {
        if(session != NULL)
            *diag = session->command_diag;
        else
            memset(diag, 0, sizeof(*diag));
    }
}

/** @brief Reset line-oriented AT command diagnostics. */
void at_session_command_diag_reset(at_session_t *session)
{
    if(session != NULL)
        memset(&session->command_diag, 0, sizeof(session->command_diag));
}

bool at_session_raw_is_active(const at_session_t *session)
{
    return session && (session->raw_waiting != 0U ||
                       session->raw_done != 0U ||
                       session->client.raw_active != 0U);
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
    if(!session || !session->client.tx || !data || len == 0U ||
       session->recovery_required != 0U)
        return -1;

    if(session->client.tx(data, len, session->client.tx_arg) != 0)
    {
        session->command_diag.tx_errors++;
        at_session_require_transport_reset(session);
        return -1;
    }
    return 0;
}
