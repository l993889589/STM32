#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <stdint.h>
#include <stdio.h>

typedef void (*dispatcher_handler_t)(void);

typedef struct {
    uint16_t cmd;
    dispatcher_handler_t handler;
} dispatcher_entry_t;

#define MAX_DISPATCHER_COUNT 16

// 注册接口
int dispatcher_register(uint16_t cmd, dispatcher_handler_t handler);

// 调用命令
int dispatcher_call(uint16_t cmd);

// 打印注册表
void dispatcher_print(void);

#endif