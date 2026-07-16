/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2025-06-13 13:58:46
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2025-06-13 17:38:29
 * @FilePath: \Confidential_ML307C_OpenCPU_Standard_1.0.0.25022817_beta\custom\custom_main\flash\app\src\app_uart_recv.c
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "debug_log.h"
#include "nut.h"

#include "debug_log.h"
#include "nut.h"






// 数据包接收回调
void nut_recv_callback(uint8_t uart_id, uint8_t *data, uint16_t len)
{
    // 在这里处理接收到的数据包
    // 根据不同的串口ID进行不同的处理
		debug_printf("sss\n");
    switch(uart_id) {
        case 0:
            // 处理串口0的数据
 //           bsp_uart_write(BSP_UART_0, data, len, 1000);
            break;
        case 1:
            // 处理串口1的数据
            break;
        case 2:
            // 处理串口2的数据
            break;
        default:
            break;
    }
}