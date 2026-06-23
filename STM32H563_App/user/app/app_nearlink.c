#include "app_nearlink.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#include "app_board_io.h"
#include "app_config.h"
#include "app_ldc_config.h"
#include "at_session.h"
#include "bsp.h"
#include "ldc_core.h"

static ldc_t g_ldc;
static uint8_t g_ring[APP_NEARLINK_RX_BUF_SIZE];
static ldc_packet_t g_packets[APP_NEARLINK_PACKET_COUNT];
static uint8_t g_uart_rx[APP_NEARLINK_UART_RX_BUF_SIZE];
static volatile uint8_t g_packet_pending;
static at_session_t g_session;
static at_nearlink_module_t g_module;
static at_nearlink_config_t g_config;
static char g_local_name[32] = APP_NEARLINK_SERVER_NAME;
static char g_peer_name[32] = APP_NEARLINK_SERVER_NAME;
static volatile uint8_t g_apply_pending = 1U;
static uint8_t g_send_data[256];
static volatile uint16_t g_send_len;

static uint32_t app_nearlink_now(void *arg) { (void)arg; return (uint32_t)tx_time_get(); }
static void app_nearlink_sleep(uint32_t ms, void *arg) { (void)arg; tx_thread_sleep(ms ? ms : 1U); }

static void app_nearlink_log(const char *line, void *arg)
{
    (void)arg;
    if(line)
    {
        (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
        (void)app_usb_cdc_write((const uint8_t *)"\r\n", 2U);
    }
}

static void nearlink_log(const char *fmt, ...)
{
    char buf[128];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    app_usb_cdc_write((uint8_t *)buf, strlen(buf));
}

static int app_nearlink_tx(const uint8_t *data, uint16_t len, void *arg)
{
    (void)arg;
    return bsp_uart_write(BSP_UART_NEARLINK, data, len, 1000U) == (int)len ? 0 : -1;
}

static void app_nearlink_reset(void *arg) { (void)arg; bsp_nearlink_hard_reset(100U, 3000U); }

static void app_nearlink_data(const char *peer, const uint8_t *data, uint16_t len, void *arg)
{
    char line[80];
    (void)arg;
    (void)snprintf(line, sizeof(line), "nearlink rx peer=%s len=%u\r\n", peer ? peer : "server", (unsigned int)len);
    (void)app_usb_cdc_write((const uint8_t *)line, (uint32_t)strlen(line));
    (void)app_usb_cdc_write(data, len);
    (void)app_usb_cdc_write((const uint8_t *)"\r\n", 2U);
}

static void app_nearlink_drain(void)
{
    uint8_t frame[APP_NEARLINK_LDC_MAX_FRAME];
    int len;
    while((len = ldc_read_packet(&g_ldc, frame, sizeof(frame))) > 0)
        at_session_input(&g_session, frame, (uint32_t)len);
    g_packet_pending = ldc_packet_available(&g_ldc) ? 1U : 0U;
}

static void app_nearlink_poll(void *arg)
{
    (void)arg;
    ldc_tick(&g_ldc, 1U);
    if(g_packet_pending || ldc_packet_available(&g_ldc)) app_nearlink_drain();
}

static void app_nearlink_uart_rx(bsp_uart_port_t port, const uint8_t *data, uint16_t len, void *arg)
{
    (void)port; (void)arg;
    if(data && len)
    {
        (void)ldc_write(&g_ldc, data, len);
        g_packet_pending = 1U;
    }
}

UINT app_nearlink_init(void)
{
    ldc_init(&g_ldc, g_ring, sizeof(g_ring), g_packets, APP_NEARLINK_PACKET_COUNT);
    ldc_set_mode(&g_ldc, LDC_MODE_OVERWRITE);
    app_ldc_config_apply(&g_ldc, APP_LDC_PORT_NEARLINK_AT);
    at_session_init(&g_session, app_nearlink_tx, NULL, app_nearlink_now, NULL, app_nearlink_sleep, NULL);
    at_session_set_logger(&g_session, app_nearlink_log, NULL);
    at_session_set_poll_callback(&g_session, app_nearlink_poll, NULL);
    at_nearlink_init(&g_module, &g_session, app_nearlink_reset, NULL, app_nearlink_data, NULL);
    g_config.role = (at_nearlink_role_t)APP_NEARLINK_DEFAULT_ROLE;
    g_config.local_name = g_local_name;
    g_config.local_address = APP_NEARLINK_SERVER_ADDRESS;
    g_config.peer_name = g_peer_name;
    g_config.auth_type = 0U;
    g_config.key = NULL;
    (void)bsp_uart_register_rx_callback(BSP_UART_NEARLINK, app_nearlink_uart_rx, NULL);
    return bsp_uart_start_rx(BSP_UART_NEARLINK, g_uart_rx, sizeof(g_uart_rx)) == 0 ? TX_SUCCESS : TX_START_ERROR;
}

int app_nearlink_request_role(at_nearlink_role_t role, const char *local_name, const char *peer_name)
{
    if(role != AT_NEARLINK_ROLE_CLIENT && role != AT_NEARLINK_ROLE_SERVER) return -1;
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
    if(peer_name && peer_name[0]) { strncpy(g_peer_name, peer_name, sizeof(g_peer_name)-1U); g_peer_name[sizeof(g_peer_name)-1U]='\0'; }
    g_config.role = role;
    g_config.local_address = role == AT_NEARLINK_ROLE_SERVER ?
                             APP_NEARLINK_SERVER_ADDRESS : APP_NEARLINK_CLIENT_ADDRESS;
    g_apply_pending = 1U;
    return 0;
}

int app_nearlink_request_send(const uint8_t *data, uint16_t len)
{
    if(!data || !len || len > sizeof(g_send_data) || g_send_len) return -1;
    memcpy(g_send_data, data, len);
    g_send_len = len;
    return 0;
}

void app_nearlink_get_status(app_nearlink_status_t *status)
{
    if(!status) return;
    status->role = g_config.role;
    status->active = g_module.active;
    status->connected = g_module.connected;
    status->apply_pending = g_apply_pending;
    strncpy(status->local_name, g_local_name, sizeof(status->local_name));
    strncpy(status->peer_name, g_peer_name, sizeof(status->peer_name));
    status->last_error = g_module.last_error ? g_module.last_error : "none";
}

void app_nearlink_task_entry(ULONG thread_input)
{
    (void)thread_input;
    for(;;)
    {
        app_nearlink_poll(NULL);
        if(g_apply_pending)
        {
            g_apply_pending = 0U;
						bool state=at_nearlink_stop(&g_module);
						nearlink_log("state=%d", state);

            if(!at_nearlink_apply(&g_module, &g_config)) app_nearlink_log("nearlink error: role apply failed", NULL);
        }
        if(g_send_len)
        {
            uint16_t len = g_send_len;
            if(!at_nearlink_send(&g_module, g_send_data, len)) app_nearlink_log("nearlink error: send failed", NULL);
            g_send_len = 0U;
        }
        tx_thread_sleep(1U);
    }
}
