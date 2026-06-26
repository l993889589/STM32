/*
 * app_nearlink.c
 *
 * Nearlink 子系统（串口 AT 会话 + LDC 接收 + 简单协议封装）
 *
 * 概述:
 *  - 目的: 管理 Nearlink 外设的串口收发、AT 会话、LDC 数据包解析与上层应用交互。
 *  - 主要职责:
 *      * 初始化并配置 LDC（环形缓冲 + 包队列）用于接收 Nearlink 串口数据。
 *      * 将 LDC 完整包交给 at_session 解析（行级 AT 处理与 URC 分发）。
 *      * 提供会话发送回调、日志回调与 poll 回调以驱动会话工作。
 *      * 提供 Nearlink 配置、重置、发送数据与状态查询接口。
 *  - 设计假设:
 *      * LDC 的并发保护通过注入的 lock/unlock 回调（在此使用禁用中断）实现。
 *      * at_session 的阻塞等待通过注入的 now/sleep/poll 回调驱动。
 *      * 本模块以任务/线程方式运行其主循环（app_nearlink_task_entry）。
 *
 * 使用要点:
 *  - 在初始化时调用 app_nearlink_init() 注册 UART RX 回调并启动接收。
 *  - 在主循环或定时器中调用 app_nearlink_poll() 以驱动 LDC tick 与包处理（也由 poll 回调使用）。
 *  - 通过 app_nearlink_request_role/app_nearlink_request_send 提交配置或发送请求，任务循环会处理这些请求。
 */

#include "app_nearlink.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "app_board_io.h"
#include "app_config.h"
#include "app_event_bridge.h"
#include "app_ldc_config.h"
#include "at_session.h"
#include "bsp.h"
#include "ldc/ldc_endpoint_threadx.h"

/* 本文件使用的静态全局变量（模块级状态） */
static ldc_endpoint_t g_endpoint;
static uint8_t g_ring[APP_NEARLINK_RX_BUF_SIZE];
static ldc_packet_t g_packets[APP_NEARLINK_PACKET_COUNT];
static uint8_t g_uart_rx[APP_NEARLINK_UART_RX_BUF_SIZE];
static volatile uint32_t g_uart_rx_bytes;      /* 累计接收字节计数 */

/* 内部的 at_session 实例与 Nearlink 模块封装结构 */
static at_session_t g_session;
static at_nearlink_module_t g_module;
static at_nearlink_config_t g_config;
static char g_local_name[32] = APP_NEARLINK_SERVER_NAME;
static char g_peer_name[32] = APP_NEARLINK_SERVER_NAME;
static volatile uint8_t g_apply_pending = 1U;  /* 标记是否有配置待应用 */
static uint8_t g_send_data[256];
static volatile uint16_t g_send_len;           /* 待发送数据长度（0 表示无待发送） */

static void app_nearlink_drain(void);

/* 时间与睡眠回调（注入给 at_session）——此处使用 ThreadX 时间函数 */
static uint32_t app_nearlink_now(void *arg) { (void)arg; return (uint32_t)tx_time_get(); }
static void app_nearlink_sleep(uint32_t ms, void *arg)
{
    ULONG events;

    (void)arg;
    (void)ldc_endpoint_wait_for(&g_endpoint, ms ? ms : 1U, &events);
    if((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
       ldc_endpoint_packet_count(&g_endpoint) != 0U)
        app_nearlink_drain();
}

/* 会话日志回调：将会话日志行输出到 USB CDC（用于调试） */
static void app_nearlink_log(const char *line, void *arg)
{
    (void)arg;
    if(line)
    {
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
        (void)app_usb_cdc_write((const uint8_t *)"\r\n", 2U);
    }
}

/* at_session 的发送回调：将数据通过 BSP UART 同步发送（阻塞直到完成） */
static int app_nearlink_tx(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;
    return bsp_uart_write_wait_complete(BSP_UART_NEARLINK, data, len, 1000U) == (int)len ? 0 : -1;
}

/* 将 LDC 中的完整包交给 at_session 处理（把包内容作为字节流输入） */
static void app_nearlink_drain(void)
{
    uint8_t frame[APP_NEARLINK_LDC_MAX_FRAME];
    int len;
    while((len = ldc_endpoint_read(&g_endpoint, frame, sizeof(frame))) > 0)
        at_session_input(&g_session, frame, (uint32_t)len);
}

/* poll 函数：被注入到 at_session，用于在等待期间驱动 LDC tick 与包处理 */
void app_nearlink_poll(void *arg)
{
    ULONG events;

    (void)arg;
    (void)ldc_endpoint_poll(&g_endpoint, &events);
    if((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
       ldc_endpoint_packet_count(&g_endpoint) != 0U)
        app_nearlink_drain();
}

/* 硬件复位函数：拉低复位引脚并等待模块启动，同时在等待期间驱动 poll */
static void app_nearlink_reset(void *arg)
{
    uint32_t elapsed = 0U;

    (void)arg;
    bsp_nearlink_reset_assert();
    tx_thread_sleep(100);
    bsp_nearlink_reset_release();

    while(elapsed < APP_NEARLINK_BOOT_WAIT_MS)
    {
        app_nearlink_poll(NULL);
        tx_thread_sleep(1U);
        elapsed++;
    }
}

/* Nearlink 数据回调：当模块收到来自 peer 的数据时调用（由 at_nearlink 模块触发） */
static void app_nearlink_data(const char *peer, const uint8_t *data, uint16_t len, void *arg)
{
    char line[80];
    (void)arg;
    (void)snprintf(line, sizeof(line), "nearlink rx peer=%s len=%u\r\n", peer ? peer : "server", (unsigned int)len);
    app_event_link_frame(APP_MSG_SOURCE_NEARLINK, len);
    (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
    (void)app_usb_cdc_write(data, len);
    (void)app_usb_cdc_write((const uint8_t *)"\r\n", 2U);
}

/* UART RX 回调：将接收到的字节写入 LDC，并累加接收字节计数 */
static void app_nearlink_uart_rx(bsp_uart_port_t port, const uint8_t *data, uint16_t len, void *arg)
{
    (void)port;
    (void)arg;
    if(data && len)
    {
        g_uart_rx_bytes += len;
        (void)ldc_endpoint_write(&g_endpoint, data, len);
        app_event_link_activity(APP_MSG_SOURCE_NEARLINK, len);
    }
}

/* 初始化函数：配置 LDC、at_session、注册 UART 回调并启动接收 */
UINT app_nearlink_init(void)
{
    const app_ldc_port_config_t *port_config;
    ldc_endpoint_config_t endpoint_config;

    port_config = app_ldc_config_get(APP_LDC_PORT_NEARLINK_AT);
    if(!port_config)
        return TX_PTR_ERROR;

    endpoint_config.name = port_config->name;
    endpoint_config.ring_buffer = g_ring;
    endpoint_config.ring_size = sizeof(g_ring);
    endpoint_config.packet_pool = g_packets;
    endpoint_config.packet_count = APP_NEARLINK_PACKET_COUNT;
    endpoint_config.max_frame = port_config->max_frame;
    endpoint_config.timeout_ms = port_config->timeout_ms;
    endpoint_config.delimiter = port_config->delimiter;
    endpoint_config.mode = LDC_MODE_OVERWRITE;
    if(ldc_endpoint_init(&g_endpoint, &endpoint_config) != TX_SUCCESS)
        return TX_START_ERROR;

    /* 初始化 at_session：传入发送回调、时间与睡眠函数 */
    at_session_init(&g_session, app_nearlink_tx, NULL, app_nearlink_now, NULL, app_nearlink_sleep, NULL);
    /* 注册会话日志回调（输出到 USB CDC） */
    at_session_set_logger(&g_session, app_nearlink_log, NULL);
    /* 将 poll 回调注入会话，以便在等待期间驱动 LDC tick 与包处理 */
    at_session_set_poll_callback(&g_session, app_nearlink_poll, NULL);

    /* 初始化 at_nearlink 模块（协议层），传入 reset 与 data 回调 */
    at_nearlink_init(&g_module, &g_session, app_nearlink_reset, NULL, app_nearlink_data, NULL);

    /* 初始化默认配置（role、名称、地址等） */
    g_config.role = (at_nearlink_role_t)APP_NEARLINK_DEFAULT_ROLE;
    g_config.local_name = g_local_name;
    g_config.local_address = APP_NEARLINK_SERVER_ADDRESS;
    g_config.peer_name = g_peer_name;
    g_config.auth_type = 0U;
    g_config.key = NULL;

    /* 注册 UART RX 回调并启动接收缓冲 */
    (void)bsp_uart_register_rx_callback(BSP_UART_NEARLINK, app_nearlink_uart_rx, NULL);
    return bsp_uart_start_rx(BSP_UART_NEARLINK, g_uart_rx, sizeof(g_uart_rx)) == 0 ? TX_SUCCESS : TX_START_ERROR;
}

/* 外部接口：请求更改角色/名称（异步生效，由任务循环处理） */
int app_nearlink_request_role(at_nearlink_role_t role, const char *local_name, const char *peer_name)
{
    if(role != AT_NEARLINK_ROLE_CLIENT && role != AT_NEARLINK_ROLE_SERVER) return -1;

    /* 更新本地名称（若提供）或使用默认名称 */
    if(local_name && local_name[0])
    {
        strncpy(g_local_name, local_name, sizeof(g_local_name)-1U);
        g_local_name[sizeof(g_local_name)-1U]='\0';
    }
    else
    {
        const char *default_name = role == AT_NEARLINK_ROLE_SERVER ?
                                   APP_NEARLINK_SERVER_NAME : APP_NEARLINK_CLIENT_NAME;
        strncpy(g_local_name, default_name, sizeof(g_local_name)-1U);
        g_local_name[sizeof(g_local_name)-1U]='\0';
    }

    /* 更新对端名称（若提供） */
    if(peer_name && peer_name[0]) { strncpy(g_peer_name, peer_name, sizeof(g_peer_name)-1U); g_peer_name[sizeof(g_peer_name)-1U]='\0'; }

    /* 更新配置并标记为待应用 */
    g_config.role = role;
    g_config.local_address = role == AT_NEARLINK_ROLE_SERVER ?
                             APP_NEARLINK_SERVER_ADDRESS : APP_NEARLINK_CLIENT_ADDRESS;
    g_apply_pending = 1U;
    app_event_status(APP_MSG_SOURCE_NEARLINK, (uintptr_t)role);
    (void)ldc_endpoint_signal(&g_endpoint, LDC_ENDPOINT_EVT_CONTROL);
    return 0;
}

/* 外部接口：请求发送数据（非阻塞，任务循环会实际发送） */
int app_nearlink_request_send(const uint8_t *data, uint16_t len)
{
    if(!data || !len || len > sizeof(g_send_data) || g_send_len) return -1;
    memcpy(g_send_data, data, len);
    g_send_len = len;
    (void)ldc_endpoint_signal(&g_endpoint, LDC_ENDPOINT_EVT_CONTROL);
    return 0;
}

/* 获取 Nearlink 状态信息（用于 UI/诊断） */
void app_nearlink_get_status(app_nearlink_status_t *status)
{
    ldc_stats_t stats;

    if(!status) return;
    status->role = g_config.role;
    status->active = g_module.active;
    status->connected = g_module.connected;
    status->apply_pending = g_apply_pending;
    status->reset_pin = (HAL_GPIO_ReadPin(BSP_NEARLINK_RESET_PORT, BSP_NEARLINK_RESET_PIN) == GPIO_PIN_SET) ? 1U : 0U;
    strncpy(status->local_name, g_local_name, sizeof(status->local_name));
    strncpy(status->peer_name, g_peer_name, sizeof(status->peer_name));
    status->last_error = g_module.last_error ? g_module.last_error : "none";
    status->uart_rx_bytes = g_uart_rx_bytes;
    status->uart_rx_events = bsp_uart_rx_events(BSP_UART_NEARLINK);
    status->ldc_rx_bytes = 0U;
    status->ldc_packets = 0U;
    if(ldc_endpoint_get_stats(&g_endpoint, &stats))
    {
        status->ldc_rx_bytes = (uint32_t)stats.rx_bytes;
        status->ldc_packets = (uint32_t)stats.packets;
    }
}

/* Nearlink 任务入口：处理配置应用、发送请求与定期 poll */
void app_nearlink_task_entry(ULONG thread_input)
{
    ULONG events;

    (void)thread_input;

    tx_thread_sleep(10000); /* 启动延迟，给系统其他部分时间就绪 */

    for(;;)
    {
        /* 如果有配置待应用，则停止当前 nearlink 会话并应用新配置 */
        if(g_apply_pending)
        {
            g_apply_pending = 0U;
            (void)at_nearlink_stop(&g_module);
            if(!at_nearlink_apply(&g_module, &g_config))
            {
                app_event_error(APP_MSG_SOURCE_NEARLINK, 1U);
                app_nearlink_log("nearlink error: role apply failed", NULL);
            }
        }

        /* 如果有待发送数据，则调用 at_nearlink_send 发送 */
        if(g_send_len)
        {
            uint16_t len = g_send_len;
            if(!at_nearlink_send(&g_module, g_send_data, len))
            {
                app_event_error(APP_MSG_SOURCE_NEARLINK, 2U);
                app_nearlink_log("nearlink error: send failed", NULL);
            }
            g_send_len = 0U;
        }

        if(ldc_endpoint_wait(&g_endpoint, &events) == TX_SUCCESS &&
           ((events & LDC_ENDPOINT_EVT_PACKET) != 0U ||
            ldc_endpoint_packet_count(&g_endpoint) != 0U))
            app_nearlink_drain();
    }
}
