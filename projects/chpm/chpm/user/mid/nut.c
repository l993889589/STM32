/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2025-06-09 16:40:32
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2025-06-17 15:08:55
 * @FilePath: \nut\nut.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "nut.h"
#include <string.h>

// 内存分配函数指针
static nut_malloc_fn g_malloc = NULL;
static nut_free_fn g_free = NULL;

// 节点结构
typedef struct {
    size_t len;  // 当前节点实际数据长度
    char data[]; // 柔性数组，存储实际数据
} nut_node_t;



// nut结构
struct nut {
    size_t node_count;    // 节点数量
    size_t node_size;     // 单个节点最大长度
    size_t head;          // 读指针
    size_t tail;          // 写指针
    size_t current_len;   // 当前节点已写入长度
    int latency;          // 分包时间
    int current_time;     // 当前时间计数
    
    // block模式相关
    int is_block_mode;    // 是否处于block模式
    void* block_buffer;   // block模式下的临时缓冲区
    size_t block_len;     // block模式下的缓冲区长度
    size_t block_pos;     // block模式下的当前写入位置
    int block_start_time; // block开始时间
    int block_timeout;    // block超时时间
    int block_latency;    // block模式分包时间
    int first_data_time;  // 第一个数据包到达时间
    
    // 回调函数
    nut_flush_cb_t flush_cb;  // 节点结算回调
    nut_block_cb_t block_cb;  // block事件回调
    void* argv;               // 回调函数参数
    
    nut_node_t* nodes[];  // 柔性数组，存储所有节点
};

// 初始化内存分配函数
void nut_init(nut_malloc_fn malloc_fn, nut_free_fn free_fn) {
    g_malloc = malloc_fn;
    g_free = free_fn;
}

// 注册回调函数
void nut_reg(nut_t* handle, nut_flush_cb_t flush_cb, nut_block_cb_t block_cb, void* argv) {
    if (!handle) return;
    handle->flush_cb = flush_cb;
    handle->block_cb = block_cb;
    handle->argv = argv;
}

// 计算所需内存大小
size_t nut_need(size_t node_count, size_t node_size) {
    return sizeof(nut_t) + 
           node_count * sizeof(nut_node_t*) + 
           node_count * (sizeof(nut_node_t) + node_size);
}

// 创建nut实例
nut_t* nut_create(size_t node_count, size_t node_size) {
    if (!g_malloc || !g_free) return NULL;
    
    size_t total_size = nut_need(node_count, node_size);
    nut_t* nut = (nut_t*)g_malloc(total_size);
    if (!nut) return NULL;
    
    nut->node_count = node_count;
    nut->node_size = node_size;
    nut->head = 0;
    nut->tail = 0;
    nut->current_len = 0;
    nut->latency = 0;
    nut->current_time = 0;
    nut->is_block_mode = 0;
    nut->block_buffer = NULL;
    nut->block_len = 0;
    nut->block_pos = 0;
    nut->flush_cb = NULL;
    nut->block_cb = NULL;
    
    // 分配并初始化所有节点
    char* data_ptr = (char*)(nut->nodes + node_count);
    for (size_t i = 0; i < node_count; i++) {
        nut->nodes[i] = (nut_node_t*)data_ptr;
        nut->nodes[i]->len = 0;
        data_ptr += sizeof(nut_node_t) + node_size;
    }
    
    return nut;
}

// 检查是否需要结算当前节点
static void check_flush(nut_t* nut) {
    if (nut->current_len > 0 && 
        (nut->current_len >= nut->node_size || 
         (nut->latency > 0 && nut->current_time >= nut->latency))) {
        // 设置节点长度
        nut->nodes[nut->tail]->len = nut->current_len;
        
        // 触发flush回调
        if (nut->flush_cb) {
            nut->flush_cb(nut, nut->tail, nut->current_len, nut->argv);
        }
        
        // 移动到下一个节点
        nut->tail = (nut->tail + 1) % nut->node_count;
        nut->current_len = 0;
        nut->current_time = 0;
    }
}

// 添加数据
size_t nut_add(nut_t* nut, const void* data, size_t len) {
    if (!nut || !data || len == 0) return 0;
    
    // 如果处于block模式，直接写入block缓冲区
    if (nut->is_block_mode) {
        size_t space_left = nut->block_len - nut->block_pos;
        if (space_left == 0) return 0;  // 缓冲区已满
        
        size_t write_len = len;
        if (write_len > space_left) {
            write_len = space_left;
        }
        
        // 记录第一个数据包到达的时间
        if (nut->block_pos == 0) {
            nut->first_data_time = nut->current_time;
        }
        
        memcpy((char*)nut->block_buffer + nut->block_pos, data, write_len);
        nut->block_pos += write_len;
        
        // 检查是否达到最大长度
        if (nut->block_pos >= nut->block_len) {
            nut->is_block_mode = 0;
            // 触发block结束回调
            if (nut->block_cb) {
                nut->block_cb(nut, NUT_BLOCK_OVER, nut->block_pos, nut->argv);
            }
        }
        
        return write_len;
    }
    
    // 正常队列模式
    size_t total_written = 0;
    const char* src = (const char*)data;
    
    while (total_written < len) {
        // 检查是否需要结算当前节点
        check_flush(nut);
        
        // 计算当前节点剩余空间
        size_t current_node = nut->tail;
        size_t space_left = nut->node_size - nut->current_len;
        
        if (space_left == 0) {
            // 当前节点已满，移动到下一个节点
            nut->tail = (nut->tail + 1) % nut->node_count;
            nut->current_len = 0;
            nut->current_time = 0;
            continue;
        }
        
        // 计算本次写入长度
        size_t write_len = len - total_written;
        if (write_len > space_left) {
            write_len = space_left;
        }
        
        // 写入数据
        memcpy(nut->nodes[current_node]->data + nut->current_len, 
               src + total_written, write_len);
        nut->current_len += write_len;
        total_written += write_len;
        
        // 如果当前节点已满，结算它
        if (nut->current_len >= nut->node_size) {
            nut->nodes[current_node]->len = nut->current_len;
            
            // 触发flush回调
            if (nut->flush_cb) {
                nut->flush_cb(nut, current_node, nut->current_len, nut->argv);
            }
            
            nut->tail = (nut->tail + 1) % nut->node_count;
            nut->current_len = 0;
            nut->current_time = 0;
        }
    }
    
    return total_written;
}

// 读取数据
size_t nut_read(nut_t* nut, void* data, size_t len) {
    if (!nut || !data || len == 0) return 0;
    
    // 检查是否有数据可读
    if (nut->head == nut->tail && nut->current_len == 0) return 0;
    
    size_t read_len = nut->nodes[nut->head]->len;
    if (read_len > len) read_len = len;
    
    memcpy(data, nut->nodes[nut->head]->data, read_len);
    nut->head = (nut->head + 1) % nut->node_count;
    
    return read_len;
}

// 窥视数据
size_t nut_peek(nut_t* nut, void* data, size_t len) {
    if (!nut || !data || len == 0) return 0;
    
    // 检查是否有数据可读
    if (nut->head == nut->tail && nut->current_len == 0) return 0;
    
    size_t read_len = nut->nodes[nut->head]->len;
    if (read_len > len) read_len = len;
    
    memcpy(data, nut->nodes[nut->head]->data, read_len);
    return read_len;
}

// 时基更新
void nut_tick(nut_t* nut, int tick_ms) {
    if (!nut) return;
    
    nut->current_time += tick_ms;
    
    // 检查block模式超时
    if (nut->is_block_mode) {
        // 检查是否达到最大长度
        if (nut->block_pos >= nut->block_len) {
            nut->is_block_mode = 0;
            // 触发block结束回调
            if (nut->block_cb) {
                nut->block_cb(nut, NUT_BLOCK_OVER, nut->block_pos, nut->argv);
            }
            return;
        }
        
        // 检查是否达到分包时间
        if (nut->block_pos > 0 && nut->block_latency > 0 && 
            nut->current_time - nut->first_data_time >= nut->block_latency) {
            nut->is_block_mode = 0;
            // 触发block结束回调
            if (nut->block_cb) {
                nut->block_cb(nut, NUT_BLOCK_OVER, nut->block_pos, nut->argv);
            }
            return;
        }
        
        // 检查是否超时
        if (nut->current_time - nut->block_start_time >= nut->block_timeout) {
            nut->is_block_mode = 0;
            // 触发block结束回调
            if (nut->block_cb) {
                nut->block_cb(nut, NUT_BLOCK_OVER, nut->block_pos, nut->argv);
            }
            return;
        }
    }
    
    check_flush(nut);
}

// 设置分包时间
void nut_latency(nut_t* nut, int tick_ms) {
    if (!nut) return;
    nut->latency = tick_ms;
}

// 手动结算当前节点
void nut_flush(nut_t* nut) {
    if (!nut || nut->current_len == 0) return;
    
    nut->nodes[nut->tail]->len = nut->current_len;
    
    // 触发flush回调
    if (nut->flush_cb) {
        nut->flush_cb(nut, nut->tail, nut->current_len, nut->argv);
    }
    
    nut->tail = (nut->tail + 1) % nut->node_count;
    nut->current_len = 0;
    nut->current_time = 0;
}

// 阻塞读取
int nut_block(nut_t* nut, void* data, size_t len, int timeout_ms, int latency_ms) {
    if (!nut || !data || len == 0) return 0;
    
    // 进入block模式
    nut->is_block_mode = 1;
    nut->block_buffer = data;
    nut->block_len = len;
    nut->block_pos = 0;
    nut->block_start_time = nut->current_time;
    nut->block_timeout = timeout_ms;
    nut->block_latency = latency_ms;
    nut->first_data_time = 0;  // 重置第一个数据包时间
    
    // 触发block开始回调
    if (nut->block_cb) {
        nut->block_cb(nut, NUT_BLOCK_START, 0, nut->argv);
    }
    
    // 等待数据
    while (nut->is_block_mode) {
        // 触发blocking回调
        if (nut->block_cb) {
            nut->block_cb(nut, NUT_BLOCKING, nut->block_pos, nut->argv);
        }
    }
    
    return nut->block_pos > 0 ? 1 : 0;
}

// 销毁实例
void nut_destroy(nut_t* nut) {
    if (nut && g_free) {
        g_free(nut);
    }
}
