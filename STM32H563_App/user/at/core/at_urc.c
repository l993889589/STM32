/*
 * at_urc.c
 *
 * URC（Unsolicited Result Code）表管理与分发
 *
 * 概述:
 *  - 目的: 管理一组以字符串前缀匹配的 URC 处理器，并在接收到行时按前缀分发给对应处理函数。
 *  - 设计假设:
 *      * URC 表大小由 AT_URC_MAX_ENTRIES 在头文件中定义并固定。
 *      * 前缀字符串由调用者提供并在注册后保持有效（未复制）。
 *      * 分发函数在匹配到第一个前缀后调用对应处理器并返回，不继续查找其他匹配项。
 *  - 主要职责:
 *      * 初始化 URC 表（清零计数）。
 *      * 注册新的 URC 条目（前缀 + 处理函数 + 用户参数）。
 *      * 对输入行进行前缀匹配并调用相应处理器。
 *
 * 使用示例:
 *  at_urc_init(&table);
 *  at_urc_register(&table, "+CMTI:", sms_ind_handler, handler_arg);
 *  ...
 *  if(at_urc_dispatch(&table, line))  line 被 URC 处理器消费 *
 *
 * 注意事项:
 *  - 注册时不复制 prefix 指针，调用者必须保证 prefix 在表项有效期内不被释放或修改。
 *  - dispatch 返回 1 表示某个 URC 处理器已处理该行，返回 0 表示未匹配任何 URC。
 */

#include "at_urc.h"

#include <string.h>

/* at_urc_init
 *
 * 初始化 URC 表结构，将条目计数置为 0。
 * 参数:
 *  - table: 指向 at_urc_table_t 的指针（必须有效）
 *
 * 该函数是幂等的：多次调用效果相同。
 */
void at_urc_init(at_urc_table_t *table)
{
    if(!table)
        return;

    table->count = 0U;
}

/* at_urc_register
 *
 * 在 URC 表中注册一个新的前缀处理器。
 *
 * 参数:
 *  - table: URC 表指针
 *  - prefix: 要匹配的前缀字符串（不复制，调用者需保证其生命周期）
 *  - handler: 匹配到前缀时调用的回调函数，签名为 void (*)(const char *line, void *arg)
 *  - arg: 传递给 handler 的用户参数
 *
 * 返回值:
 *  0  : 注册成功
 *  -1 : 参数无效（table/prefix/handler 为空）
 *  -2 : 表已满（超过 AT_URC_MAX_ENTRIES）
 *
 * 注意:
 *  - 不检查重复前缀；若需要可在外部先查询或在此扩展检查逻辑。
 */
int at_urc_register(at_urc_table_t *table, const char *prefix, at_urc_handler_t handler, void *arg)
{
    if(!table || !prefix || !handler)
        return -1;

    if(table->count >= AT_URC_MAX_ENTRIES)
        return -2;

    table->entries[table->count].prefix = prefix;
    table->entries[table->count].handler = handler;
    table->entries[table->count].arg = arg;
    table->count++;

    return 0;
}

/* at_urc_dispatch
 *
 * 对传入的一行进行前缀匹配并分发给第一个匹配的 URC 处理器。
 *
 * 参数:
 *  - table: URC 表指针
 *  - line: 要匹配的行（NUL 终止字符串）
 *
 * 返回值:
 *  1 : 找到匹配并已调用对应 handler（表示该行被 URC 消耗）
 *  0 : 未匹配任何 URC（调用者可继续将该行作为命令响应处理）
 *
 * 行为细节:
 *  - 逐条遍历已注册的前缀，使用 strncmp 进行前缀比较。
 *  - 一旦匹配，立即调用 handler 并返回，不继续查找后续匹配。
 *  - 若 prefix 为空或 table/line 为 NULL，直接返回 0。
 */
int at_urc_dispatch(at_urc_table_t *table, const char *line)
{
    if(!table || !line)
        return 0;

    for(uint8_t i = 0U; i < table->count; i++)
    {
        const char *prefix = table->entries[i].prefix;
        size_t prefix_len = strlen(prefix);

        if(strncmp(line, prefix, prefix_len) == 0)
        {
            table->entries[i].handler(line, table->entries[i].arg);
            return 1;
        }
    }

    return 0;
}
