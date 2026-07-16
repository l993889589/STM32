#ifndef NUT_H
#define NUT_H

#include <stddef.h>

typedef struct nut nut_t;

// 内存分配函数类型定义
typedef void* (*nut_malloc_fn)(size_t size);
typedef void (*nut_free_fn)(void* ptr);

// block事件定义
typedef enum {
    NUT_BLOCK_START = 1,  // block开始
    NUT_BLOCKING = 2,     // block进行中
    NUT_BLOCK_OVER = 3    // block结束
} nut_block_event_t;

// 回调函数类型定义
typedef void (*nut_flush_cb_t)(nut_t* handle, size_t node_index, size_t data_len, void* argv);
typedef void (*nut_block_cb_t)(nut_t* handle, nut_block_event_t event, size_t data_len, void* argv);

// 初始化内存分配函数
void nut_init(nut_malloc_fn malloc_fn, nut_free_fn free_fn);

// 注册回调函数
void nut_reg(nut_t* handle, nut_flush_cb_t flush_cb, nut_block_cb_t block_cb, void* argv);

// 计算所需内存大小
size_t nut_need(size_t node_count, size_t node_size);

// 创建nut实例
nut_t* nut_create(size_t node_count, size_t node_size);

// 获取节点大小
size_t nut_get_node_size(nut_t* handle);

// 添加数据
size_t nut_add(nut_t* handle, const void* data, size_t len);

// 读取数据
size_t nut_read(nut_t* handle, void* data, size_t len);

// 窥视数据
size_t nut_peek(nut_t* handle, void* data, size_t len);

// 时基更新
void nut_tick(nut_t* handle, int tick_ms);

// 设置分包时间
void nut_latency(nut_t* handle, int tick_ms);

// 手动结算当前节点
void nut_flush(nut_t* handle);

// 阻塞读取
// 返回值：1-正常分包完成，2-达到最大长度，0-超时
int nut_block(nut_t* handle, void* data, size_t len, int timeout_ms, int latency_ms);

// 销毁实例
void nut_destroy(nut_t* handle);

#endif // NUT_H
