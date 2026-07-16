/**
 * @file bsp_system.h
 * @brief Public system startup, delay and periodic BSP interfaces.
 */

/*
*********************************************************************************************************
*
*	模块名称 : STM32F4 system BSP
*	文件名称 : bsp_system.h
*	版    本 : V1.0
*	说    明 : 系统时钟、BSP启动和错误处理接口。
*	修改记录 :
*		版本号  日期         作者       说明
*		V1.0    2018-07-29  Eric2013   正式发布
*
*	Copyright (C), 2018-2030, 安富莱电子 www.armfly.com
*
*********************************************************************************************************
*/

#ifndef BSP_SYSTEM_H
#define BSP_SYSTEM_H

#include <stdint.h>

/* 提供给其他C文件调用的函数 */
void bsp_Init(void);
void bsp_Idle(void);
void System_Init(void);

void bsp_GetCpuID(uint32_t *_id);
void BSP_Error_Handler(char *file, uint32_t line);

#endif

/***************************** 安富莱电子 www.armfly.com (END OF FILE) *********************************/
