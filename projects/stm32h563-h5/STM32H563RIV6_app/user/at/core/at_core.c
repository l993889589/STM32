/*
 * at_core.c
 *
 * 轻量级 AT 命令客户端核心
 *
 * 概述:
 *  - 目的: 将串口/流式输入解析为行，路由 URC（异步通知），收集命令响应，
 *    处理回显、超时与缓冲溢出。面向嵌入式环境：无动态分配、开销小。
 *  - 设计假设:
 *      * at_client_t 实例由调用者管理，调用者负责并发安全。
 *      * 接收字节流可在中断或任务上下文调用 at_client_input。
 *      * at_client_send 的 cmd 指针在 pending 状态期间必须保持有效（调用者负责）。
 *      * 时间基准为 32-bit 毫秒计时器，使用环绕安全比较。
 *  - 主要职责:
 *      * 将字节流组装为行并调用 at_handle_line 处理。
 *      * 优先将行交给 URC 子系统处理（at_urc_dispatch）。
 *      * 收集命令响应到内部 response 缓冲并识别最终 OK/ERROR。
 *      * 提供轮询超时检查接口 at_client_poll。
 *
 * 使用摘要:
 *  1. at_client_init(&at, tx_cb, tx_arg);
 *  2. 可选: at_client_register_urc(&at, prefix, handler, arg);
 *  3. 发送: at_client_send(&at, "AT+CMD", now_ms, timeout_ms);
 *  4. 在串口接收回调/ISR 中调用: at_client_input(&at, data, len);
 *  5. 在主循环/定时器中调用: at_client_poll(&at, now_ms) 检查超时/结果。
 *  6. 读取响应: at_client_response(&at) / at_client_response_len(&at)
 *
 * 重要提示:
 *  - active_cmd 仅保存外部指针，不会复制；若需更安全请改为内部复制。
 *  - 本模块不做内部锁；并发访问需由调用者保证（例如在发送/读取时禁用中断）。
 */

#include "at_core.h"

#include <string.h>

/* at_time_after_or_equal
 *
 * 判断 now 是否大于等于 deadline，使用有符号差值以安全处理 32 位计时器环绕。
 * 这是嵌入式毫秒计时器常用的比较模式。
 */
static uint32_t at_time_after_or_equal(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/* at_is_final_ok
 *
 * 判断解析到的一行是否为最终的 "OK" 响应（精确匹配）。
 */
static int at_is_final_ok(const char *line)
{
    return strcmp(line, "OK") == 0;
}

/* at_is_final_error
 *
 * 判断解析到的一行是否表示最终错误。识别形式：
 *  - "ERROR"
 *  - "+CME ERROR: ..."（常见调制解调器错误）
 *  - "+CMS ERROR: ..."（短信相关错误）
 *
 * 使用前缀匹配来检测 +CME/+CMS 形式。
 */
static int at_is_final_error(const char *line)
{
    return strcmp(line, "ERROR") == 0 ||
           strncmp(line, "+CME ERROR:", 11U) == 0 ||
           strncmp(line, "+CMS ERROR:", 11U) == 0;
}

/* at_append_response
 *
 * 将一行追加到客户端的 response 缓冲区。
 * 每行以 '\n' 结尾，并保持以 NUL 终止。
 * 若追加会超出 AT_RESPONSE_MAX_LEN，则设置 at->result = AT_RESULT_OVERFLOW 并不追加。
 *
 * 副作用:
 *  - 更新 at->response_len 和 at->response。
 *  - 溢出时设置 at->result 为 AT_RESULT_OVERFLOW。
 */
static void at_append_response(at_client_t *at, const char *line)
{
    size_t line_len = strlen(line);

    if(at->response_len + line_len + 2U >= AT_RESPONSE_MAX_LEN)
    {
        at->result = AT_RESULT_OVERFLOW;
        return;
    }

    memcpy(&at->response[at->response_len], line, line_len);
    at->response_len = (uint16_t)(at->response_len + line_len);
    at->response[at->response_len++] = '\n';
    at->response[at->response_len] = '\0';
}

/* at_handle_line
 *
 * 每行处理核心。由 at_client_input 在组装出完整行后调用。
 *
 * 处理顺序:
 *  1. 忽略空行。
 *  2. 调用用户提供的行回调（若存在）。
 *  3. 优先交给 URC 分发器处理；若 URC 消耗该行则返回。
 *  4. 若客户端已处于终态（非 PENDING），忽略后续行。
 *  5. 若回显开启且该行等于 active_cmd，则视为回显并忽略。
 *  6. 将行追加到响应缓冲并检测最终 OK/ERROR 来设置结果。
 *
 * 并发语义:
 *  - 本函数不做内部锁保护。若在 ISR 中调用，调用者需保证与其他上下文的同步。
 */
static void at_handle_line(at_client_t *at, char *line)
{
    if(line[0] == '\0')
        return;

    if(at->line_cb)
        at->line_cb(line, at->line_arg);

    /* URC 在命令响应收集之前路由处理 */
    if(at_urc_dispatch(&at->urc, line))
        return;

    if(at->result != AT_RESULT_PENDING)
        return;

    if(at->echo_enabled && at->active_cmd && strcmp(line, at->active_cmd) == 0)
        return;

    at_append_response(at, line);

    if(at_is_final_ok(line))
        at->result = AT_RESULT_OK;
    else if(at_is_final_error(line))
        at->result = AT_RESULT_ERROR;
}

/* at_client_init
 *
 * 初始化 at_client_t 实例。清零结构体，设置发送回调与默认标志，并初始化 URC 子系统。
 *
 * 参数:
 *  - at: 指向客户端结构体（必须有效）
 *  - tx: 用于发送字节到调制解调器的回调
 *  - tx_arg: 传递给 tx 回调的用户参数
 *
 * 初始化后:
 *  - at->result 为 AT_RESULT_NONE
 *  - at->echo_enabled 默认为开启（1）
 *
 * 注意: 该函数应在系统初始化时由任务上下文调用。
 */
void at_client_init(at_client_t *at, at_tx_cb_t tx, void *tx_arg)
{
    if(!at)
        return;

    memset(at, 0, sizeof(*at));
    at->tx = tx;
    at->tx_arg = tx_arg;
    at->result = AT_RESULT_NONE;
    at->echo_enabled = 1U;
    at_urc_init(&at->urc);
}

/* at_client_set_line_callback
 *
 * 注册一个回调，该回调会在每次解析到行时被调用（在 URC 分发和响应收集之前）。
 * 可用于日志记录或额外解析。
 */
void at_client_set_line_callback(at_client_t *at, at_line_cb_t cb, void *arg)
{
    if(!at)
        return;

    at->line_cb = cb;
    at->line_arg = arg;
}

/* at_client_register_urc
 *
 * 使用前缀注册 URC 处理器，委托给 URC 子系统。
 * 出错返回负值。
 */
int at_client_register_urc(at_client_t *at, const char *prefix, at_urc_handler_t handler, void *arg)
{
    if(!at)
        return -1;

    return at_urc_register(&at->urc, prefix, handler, arg);
}

/* at_client_send
 *
 * 发送 AT 命令字符串并进入 PENDING 状态等待响应。
 *
 * 行为与检查:
 *  - 校验参数并确保当前没有挂起命令。
 *  - 使用临时缓冲 out 附加 CRLF。
 *  - 清空之前的 response 缓冲，设置 active_cmd 指针（不复制）。
 *  - 设置 deadline_ms = now_ms + timeout_ms 并将 result 置为 AT_RESULT_PENDING。
 *  - 调用 tx 回调发送字节；若 tx 失败，将 result 置为 AT_RESULT_ERROR。
 *
 * 返回码:
 *  0   : 成功（命令已发送）
 *  -1  : 参数无效
 *  -2  : 客户端忙（已有命令挂起）
 *  -3  : tx 回调失败
 *  -4  : 命令过长，超出临时缓冲
 *
 * 重要: active_cmd 保存的是传入的指针。调用者必须保证该指针在命令完成或 clear_result 之前有效。
 */
int at_client_send(at_client_t *at, const char *cmd, uint32_t now_ms, uint32_t timeout_ms)
{
    char out[AT_LINE_MAX_LEN + 2U];
    size_t cmd_len;

    if(!at || !cmd || !at->tx)
        return -1;

    if(at->result == AT_RESULT_PENDING)
        return -2;

    cmd_len = strlen(cmd);
    if(cmd_len + 2U > sizeof(out))
        return -4;

    at->response_len = 0U;
    at->response[0] = '\0';
    at_client_raw_cancel(at);
    at->active_cmd = cmd;
    at->deadline_ms = now_ms + timeout_ms;
    at->result = AT_RESULT_PENDING;

    memcpy(out, cmd, cmd_len);
    out[cmd_len++] = '\r';
    out[cmd_len++] = '\n';

    if(at->tx((const uint8_t *)out, (uint16_t)cmd_len, at->tx_arg) != 0)
    {
        at->result = AT_RESULT_ERROR;
        return -3;
    }

    return 0;
}

/* at_client_input
 *
 * 将接收到的字节输入到 AT 客户端。该函数将字节组装为行并在遇到完整行时调用 at_handle_line。
 *
 * 规则:
 *  - 忽略 CR ('\r')。
 *  - LF ('\n') 终止当前行；将行 NUL 终止并传给 at_handle_line。
 *  - 行首单独的 '>' 字符被视为独立提示行（用于 SMS 输入提示）。
 *  - 若行长度超过 AT_LINE_MAX_LEN，则丢弃该行并将客户端结果置为 AT_RESULT_OVERFLOW。
 *
 * 并发:
 *  - 设计为可在 ISR 或任务上下文调用，但调用者需保证对同一 at_client_t 的其他访问已同步。
 */
void at_client_input(at_client_t *at, const uint8_t *data, uint32_t len)
{
    if(!at || !data)
        return;

    for(uint32_t i = 0U; i < len; i++)
    {
        char ch = (char)data[i];

        if(at->raw_active != 0U)
        {
            if(at->raw_skip_eol != 0U)
            {
                if(ch == '\r')
                {
                    at->raw_skip_eol = 2U;
                    continue;
                }
                if(ch == '\n')
                {
                    at->raw_skip_eol = 0U;
                    continue;
                }
                at->raw_skip_eol = 0U;
            }

            at->raw_buffer[at->raw_received++] = (uint8_t)ch;
            if(at->raw_received >= at->raw_expected)
            {
                at->raw_active = 0U;
                at->raw_done = 1U;
            }
            continue;
        }

        if(ch == '\r')
            continue;

        if(ch == '\n')
        {
            at->line_buf[at->line_len] = '\0';
            at_handle_line(at, at->line_buf);
            at->line_len = 0U;
            continue;
        }

        if(ch == '>' && at->line_len == 0U)
        {
            at->line_buf[0] = '>';
            at->line_buf[1] = '\0';
            at_handle_line(at, at->line_buf);
            continue;
        }

        if(at->line_len + 1U >= AT_LINE_MAX_LEN)
        {
            at->line_len = 0U;
            at->result = AT_RESULT_OVERFLOW;
            continue;
        }

        at->line_buf[at->line_len++] = ch;
    }
}

/* at_client_poll
 *
 * 轮询接口用于检查命令超时。调用者应定期（例如主循环或定时器）传入当前毫秒时间。
 *
 * 若客户端处于 PENDING 且 now >= deadline，则将 result 置为 AT_RESULT_TIMEOUT。
 * 返回当前的 at_result_t 值。
 */
at_result_t at_client_poll(at_client_t *at, uint32_t now_ms)
{
    if(!at)
        return AT_RESULT_ERROR;

    if(at->result == AT_RESULT_PENDING && at_time_after_or_equal(now_ms, at->deadline_ms))
        at->result = AT_RESULT_TIMEOUT;

    return at->result;
}

/* at_client_is_busy
 *
 * 便捷访问器，检查是否有命令正在等待响应（PENDING）。
 */
bool at_client_is_busy(const at_client_t *at)
{
    return at && at->result == AT_RESULT_PENDING;
}

/* at_client_clear_result
 *
 * 清除客户端的结果状态和 active_cmd 指针。处理完完成的命令后调用以准备下一个命令。
 *
 * 注意: 该函数不会清空 response 缓冲；调用者应在调用 clear_result 之前读取响应（如需要）。
 */
void at_client_clear_result(at_client_t *at)
{
    if(!at)
        return;

    at->result = AT_RESULT_NONE;
    at->active_cmd = NULL;
}

/* at_client_response / at_client_response_len
 *
 * 访问已累积的响应缓冲及其长度。
 * 若 at 为 NULL，response() 返回空字符串，response_len() 返回 0。
 */
const char *at_client_response(const at_client_t *at)
{
    return at ? at->response : "";
}

uint16_t at_client_response_len(const at_client_t *at)
{
    return at ? at->response_len : 0U;
}

/* at_client_raw_begin_ex
 *
 * Arms a bounded binary receive window. While active, incoming bytes are copied
 * directly to buffer and are not interpreted as AT lines. This is intended for
 * AT commands whose response is "text header + exact-length binary payload",
 * such as W800 SKRCV or cellular modem data-read commands. The separator policy
 * is explicit because W800 adds an empty line while many cellular modules do
 * not; keeping that quirk out of the generic default preserves leading CR/LF
 * payload bytes on other modules.
 */
bool at_client_raw_begin_ex(at_client_t *at,
                            uint8_t *buffer,
                            uint16_t expected,
                            at_raw_separator_t separator)
{
    if(!at || !buffer || expected == 0U ||
       (separator != AT_RAW_SEPARATOR_NONE && separator != AT_RAW_SEPARATOR_EMPTY_LINE))
        return false;

    at->raw_buffer = buffer;
    at->raw_expected = expected;
    at->raw_received = 0U;
    at->raw_active = 1U;
    at->raw_done = 0U;
    at->raw_skip_eol = separator == AT_RAW_SEPARATOR_EMPTY_LINE ? 1U : 0U;
    return true;
}

bool at_client_raw_begin(at_client_t *at, uint8_t *buffer, uint16_t expected)
{
    return at_client_raw_begin_ex(at, buffer, expected, AT_RAW_SEPARATOR_NONE);
}

void at_client_raw_cancel(at_client_t *at)
{
    if(!at)
        return;

    at->raw_buffer = NULL;
    at->raw_expected = 0U;
    at->raw_received = 0U;
    at->raw_active = 0U;
    at->raw_done = 0U;
    at->raw_skip_eol = 0U;
}

bool at_client_raw_is_active(const at_client_t *at)
{
    return at && at->raw_active != 0U;
}

bool at_client_raw_is_done(const at_client_t *at)
{
    return at && at->raw_done != 0U;
}

uint16_t at_client_raw_received(const at_client_t *at)
{
    return at ? at->raw_received : 0U;
}
