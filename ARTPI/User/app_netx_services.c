#include "includes.h"
#include "app_device_config.h"
#include "app_modbus_rtu.h"
#include "app_netx_services.h"

#include <stdlib.h>
#include <string.h>

#define APP_NETX_ECHO_STACK_SIZE       2048U
#define APP_NETX_HTTP_STACK_SIZE       4096U
#define APP_NETX_SERVICE_PRIORITY         5U
#define APP_NETX_LISTEN_QUEUE_SIZE        2U
#define APP_NETX_TCP_WINDOW_SIZE       2048U
#define APP_NETX_REQUEST_BUFFER_SIZE   1024U
#define APP_NETX_HTTP_BODY_SIZE         512U
#define APP_NETX_HTTP_HEADER_SIZE       192U

static TX_THREAD netx_echo_thread;
static TX_THREAD netx_http_thread;
static NX_TCP_SOCKET netx_echo_socket;
static NX_TCP_SOCKET netx_http_socket;
static NX_IP *netx_service_ip;
static NX_PACKET_POOL *netx_service_packet_pool;
static uint8_t netx_services_started;

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_echo_stack[APP_NETX_ECHO_STACK_SIZE];

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_http_stack[APP_NETX_HTTP_STACK_SIZE];

static const char netx_config_page[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ART-Pi H750 Control</title><style>"
    "body{font-family:system-ui,sans-serif;background:#f4f7fb;color:#172033;"
    "margin:0;padding:24px}.card{max-width:560px;margin:auto;background:white;"
    "border-radius:16px;padding:24px;box-shadow:0 8px 30px #20305018}"
    "h1{margin-top:0;font-size:24px}.row{display:flex;justify-content:space-between;"
    "align-items:center;gap:20px;padding:14px 0;border-bottom:1px solid #e9edf3}"
    "input[type=number]{width:100px;padding:8px;border:1px solid #b8c2d1;"
    "border-radius:8px}input[type=checkbox]{width:22px;height:22px}"
    "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;"
    "background:#1769e0;color:white;font-size:16px;cursor:pointer}"
    "button:disabled{opacity:.55}.note{font-size:13px;color:#667085;line-height:1.5}"
    "#message{min-height:24px;margin-top:14px}.ok{color:#087443}.bad{color:#b42318}"
    "a{color:#1769e0}</style></head><body><main class=\"card\">"
    "<h1>ART-Pi STM32H750</h1>"
    "<p class=\"note\">ThreadX + NetX Duo local configuration</p>"
    "<div class=\"row\"><label for=\"unit\">Modbus RTU unit ID</label>"
    "<input id=\"unit\" type=\"number\" min=\"1\" max=\"247\"></div>"
    "<div class=\"row\"><label for=\"red\">Red LED</label>"
    "<input id=\"red\" type=\"checkbox\"></div>"
    "<div class=\"row\"><label for=\"buzzer\">Buzzer</label>"
    "<input id=\"buzzer\" type=\"checkbox\"></div>"
    "<button id=\"save\" onclick=\"saveConfig()\">Apply configuration</button>"
    "<div id=\"message\"></div>"
    "<p class=\"note\">The Modbus unit ID is stored in two CRC-protected W25Q128 slots. "
    "LED and buzzer are immediate outputs and are not restored after reset. "
    "<a href=\"/status\">View status JSON</a></p>"
    "</main><script>"
    "const unit=document.getElementById('unit'),red=document.getElementById('red'),"
    "buzzer=document.getElementById('buzzer'),message=document.getElementById('message'),"
    "save=document.getElementById('save');"
    "async function refresh(){try{const r=await fetch('/api/config',{cache:'no-store'});"
    "if(!r.ok)throw new Error('HTTP '+r.status);const c=await r.json();"
    "unit.value=c.modbus_unit_id;red.checked=!!c.red_led_on;"
    "buzzer.checked=!!c.buzzer_on;}catch(e){show(e.message,false);}}"
    "async function saveConfig(){const id=Number(unit.value);"
    "if(!Number.isInteger(id)||id<1||id>247){show('Unit ID must be 1..247',false);return;}"
    "save.disabled=true;show('Applying...',true);try{const r=await fetch('/api/config',"
    "{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify("
    "{modbus_unit_id:id,red_led_on:red.checked?1:0,buzzer_on:buzzer.checked?1:0})});"
    "const result=await r.json();if(!r.ok||!result.ok)throw new Error(result.error||"
    "('HTTP '+r.status));show('Applied; Modbus unit ID saved to W25Q128',true);"
    "setTimeout(refresh,150);}catch(e){show(e.message,false);}finally{save.disabled=false;}}"
    "function show(text,ok){message.textContent=text;message.className=ok?'ok':'bad';}"
    "refresh();</script></body></html>";

static void app_netx_echo_thread_entry(ULONG input);
static void app_netx_http_thread_entry(ULONG input);
static void app_netx_relisten(NX_TCP_SOCKET *socket, UINT port);
static UINT app_netx_http_receive_request(char *request,
                                          size_t request_size,
                                          size_t *request_length);
static size_t app_netx_http_content_length(const char *request);
static void app_netx_http_process(const char *request, size_t request_length);
static void app_netx_http_send_config(void);
static void app_netx_http_update_config(const char *request);
static uint8_t app_netx_json_get_uint(const char *json,
                                      const char *key,
                                      uint32_t *value);
static UINT app_netx_http_send(const char *status,
                               const char *content_type,
                               const char *body);
static void app_netx_http_build_status(char *body, size_t body_size);
static void app_netx_format_ipv4(ULONG address, char *text, size_t text_size);

UINT app_netx_services_start(NX_IP *ip, NX_PACKET_POOL *packet_pool)
{
    UINT status;

    if (netx_services_started != 0U)
    {
        return NX_SUCCESS;
    }
    if ((ip == NX_NULL) || (packet_pool == NX_NULL))
    {
        return NX_PTR_ERROR;
    }

    netx_service_ip = ip;
    netx_service_packet_pool = packet_pool;

    status = nx_tcp_socket_create(netx_service_ip,
                                  &netx_echo_socket,
                                  "tcp_echo",
                                  NX_IP_NORMAL,
                                  NX_FRAGMENT_OKAY,
                                  NX_IP_TIME_TO_LIVE,
                                  APP_NETX_TCP_WINDOW_SIZE,
                                  NX_NULL,
                                  NX_NULL);
    if (status != NX_SUCCESS)
    {
        return status;
    }

    status = nx_tcp_server_socket_listen(netx_service_ip,
                                         APP_NETX_TCP_ECHO_PORT,
                                         &netx_echo_socket,
                                         APP_NETX_LISTEN_QUEUE_SIZE,
                                         NX_NULL);
    if (status != NX_SUCCESS)
    {
        (void)nx_tcp_socket_delete(&netx_echo_socket);
        return status;
    }

    status = nx_tcp_socket_create(netx_service_ip,
                                  &netx_http_socket,
                                  "http_server",
                                  NX_IP_NORMAL,
                                  NX_FRAGMENT_OKAY,
                                  NX_IP_TIME_TO_LIVE,
                                  APP_NETX_TCP_WINDOW_SIZE,
                                  NX_NULL,
                                  NX_NULL);
    if (status != NX_SUCCESS)
    {
        (void)nx_tcp_server_socket_unlisten(netx_service_ip,
                                            APP_NETX_TCP_ECHO_PORT);
        (void)nx_tcp_socket_delete(&netx_echo_socket);
        return status;
    }

    status = nx_tcp_server_socket_listen(netx_service_ip,
                                         APP_NETX_HTTP_PORT,
                                         &netx_http_socket,
                                         APP_NETX_LISTEN_QUEUE_SIZE,
                                         NX_NULL);
    if (status != NX_SUCCESS)
    {
        (void)nx_tcp_socket_delete(&netx_http_socket);
        (void)nx_tcp_server_socket_unlisten(netx_service_ip,
                                            APP_NETX_TCP_ECHO_PORT);
        (void)nx_tcp_socket_delete(&netx_echo_socket);
        return status;
    }

    status = tx_thread_create(&netx_echo_thread,
                              "tcp_echo",
                              app_netx_echo_thread_entry,
                              0UL,
                              netx_echo_stack,
                              sizeof(netx_echo_stack),
                              APP_NETX_SERVICE_PRIORITY,
                              APP_NETX_SERVICE_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_DONT_START);
    if (status != TX_SUCCESS)
    {
        return status;
    }

    status = tx_thread_create(&netx_http_thread,
                              "http_server",
                              app_netx_http_thread_entry,
                              0UL,
                              netx_http_stack,
                              sizeof(netx_http_stack),
                              APP_NETX_SERVICE_PRIORITY,
                              APP_NETX_SERVICE_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_DONT_START);
    if (status != TX_SUCCESS)
    {
        (void)tx_thread_delete(&netx_echo_thread);
        return status;
    }

    netx_services_started = 1U;
    (void)tx_thread_resume(&netx_echo_thread);
    (void)tx_thread_resume(&netx_http_thread);
    bsp_uart_write_string(BSP_UART_DEBUG,
                          "NetX services ready: TCP echo port 7, HTTP port 80\r\n");
    return NX_SUCCESS;
}

static void app_netx_echo_thread_entry(ULONG input)
{
    (void)input;

    while (1)
    {
        UINT status = nx_tcp_server_socket_accept(&netx_echo_socket,
                                                  NX_WAIT_FOREVER);

        if (status == NX_SUCCESS)
        {
            NX_PACKET *packet;

            while (nx_tcp_socket_receive(&netx_echo_socket,
                                         &packet,
                                         NX_WAIT_FOREVER) == NX_SUCCESS)
            {
                status = nx_tcp_socket_send(&netx_echo_socket,
                                            packet,
                                            NX_IP_PERIODIC_RATE);
                if ((status != NX_SUCCESS) && (packet != NX_NULL))
                {
                    nx_packet_release(packet);
                    break;
                }
            }
        }

        (void)nx_tcp_socket_disconnect(&netx_echo_socket,
                                       NX_IP_PERIODIC_RATE);
        (void)nx_tcp_server_socket_unaccept(&netx_echo_socket);
        app_netx_relisten(&netx_echo_socket, APP_NETX_TCP_ECHO_PORT);
    }
}

static void app_netx_http_thread_entry(ULONG input)
{
    (void)input;

    while (1)
    {
        UINT status = nx_tcp_server_socket_accept(&netx_http_socket,
                                                  NX_WAIT_FOREVER);

        if (status == NX_SUCCESS)
        {
            char request[APP_NETX_REQUEST_BUFFER_SIZE];
            size_t request_length;

            status = app_netx_http_receive_request(request,
                                                   sizeof(request),
                                                   &request_length);
            if (status == NX_SUCCESS)
            {
                app_netx_http_process(request, request_length);
            }
            else if (status == NX_SIZE_ERROR)
            {
                (void)app_netx_http_send("413 Payload Too Large",
                                         "application/json; charset=utf-8",
                                         "{\"ok\":false,\"error\":\"request too large\"}\r\n");
            }
            else
            {
                (void)app_netx_http_send("400 Bad Request",
                                         "application/json; charset=utf-8",
                                         "{\"ok\":false,\"error\":\"incomplete request\"}\r\n");
            }
        }

        (void)nx_tcp_socket_disconnect(&netx_http_socket,
                                       NX_IP_PERIODIC_RATE);
        (void)nx_tcp_server_socket_unaccept(&netx_http_socket);
        app_netx_relisten(&netx_http_socket, APP_NETX_HTTP_PORT);
    }
}

static void app_netx_relisten(NX_TCP_SOCKET *socket, UINT port)
{
    UINT status;

    do
    {
        status = nx_tcp_server_socket_relisten(netx_service_ip,
                                               port,
                                               socket);
        if ((status == NX_SUCCESS) || (status == NX_CONNECTION_PENDING))
        {
            return;
        }

        tx_thread_sleep(NX_IP_PERIODIC_RATE);
    }
    while (1);
}

static UINT app_netx_http_receive_request(char *request,
                                          size_t request_size,
                                          size_t *request_length)
{
    size_t total_length = 0U;
    size_t expected_length = 0U;
    uint8_t header_complete = 0U;

    if ((request == NULL) || (request_size < 2U) || (request_length == NULL))
    {
        return NX_PTR_ERROR;
    }

    while (1)
    {
        NX_PACKET *packet;
        ULONG copied = 0UL;
        ULONG available;
        UINT status = nx_tcp_socket_receive(&netx_http_socket,
                                            &packet,
                                            5UL * NX_IP_PERIODIC_RATE);

        if (status != NX_SUCCESS)
        {
            return status;
        }

        available = packet->nx_packet_length;
        if (available > (ULONG)(request_size - total_length - 1U))
        {
            nx_packet_release(packet);
            return NX_SIZE_ERROR;
        }

        status = nx_packet_data_extract_offset(packet,
                                               0UL,
                                               &request[total_length],
                                               available,
                                               &copied);
        nx_packet_release(packet);
        if (status != NX_SUCCESS)
        {
            return status;
        }

        total_length += (size_t)copied;
        request[total_length] = '\0';

        if (header_complete == 0U)
        {
            const char *header_end = strstr(request, "\r\n\r\n");

            if (header_end != NULL)
            {
                size_t header_length =
                    (size_t)(header_end - request) + 4U;
                size_t content_length = app_netx_http_content_length(request);

                if (content_length > (request_size - header_length - 1U))
                {
                    return NX_SIZE_ERROR;
                }
                expected_length = header_length + content_length;
                header_complete = 1U;
            }
        }

        if ((header_complete != 0U) && (total_length >= expected_length))
        {
            *request_length = total_length;
            return NX_SUCCESS;
        }
    }
}

static size_t app_netx_http_content_length(const char *request)
{
    const char *field = strstr(request, "Content-Length:");
    char *end;
    unsigned long length;

    if (field == NULL)
    {
        return 0U;
    }

    field += strlen("Content-Length:");
    while ((*field == ' ') || (*field == '\t'))
    {
        field++;
    }
    length = strtoul(field, &end, 10);
    if ((end == field) || (length > (unsigned long)APP_NETX_REQUEST_BUFFER_SIZE))
    {
        return APP_NETX_REQUEST_BUFFER_SIZE;
    }

    return (size_t)length;
}

static void app_netx_http_process(const char *request, size_t request_length)
{
    (void)request_length;

    if ((strncmp(request, "GET /status ", 12U) == 0) ||
        (strncmp(request, "GET /api/status ", 16U) == 0))
    {
        char body[APP_NETX_HTTP_BODY_SIZE];

        app_netx_http_build_status(body, sizeof(body));
        (void)app_netx_http_send("200 OK",
                                 "application/json; charset=utf-8",
                                 body);
    }
    else if (strncmp(request, "GET /api/config ", 16U) == 0)
    {
        app_netx_http_send_config();
    }
    else if (strncmp(request, "POST /api/config ", 17U) == 0)
    {
        app_netx_http_update_config(request);
    }
    else if (strncmp(request, "GET / ", 6U) == 0)
    {
        (void)app_netx_http_send("200 OK",
                                 "text/html; charset=utf-8",
                                 netx_config_page);
    }
    else
    {
        (void)app_netx_http_send("404 Not Found",
                                 "text/plain; charset=utf-8",
                                 "Not Found\r\n");
    }
}

static void app_netx_http_send_config(void)
{
    app_modbus_rtu_config_t config;
    char body[192];

    if (app_modbus_rtu_get_config(&config) != HAL_OK)
    {
        (void)app_netx_http_send("503 Service Unavailable",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"Modbus service unavailable\"}\r\n");
        return;
    }

    (void)snprintf(body,
                   sizeof(body),
                   "{\"ok\":true,\"modbus_unit_id\":%u,"
                   "\"red_led_on\":%u,\"buzzer_on\":%u,"
                   "\"modbus_unit_id_persistent\":true}\r\n",
                   (unsigned int)config.unit_id,
                   (unsigned int)config.red_led_on,
                   (unsigned int)config.buzzer_on);
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             body);
}

static void app_netx_http_update_config(const char *request)
{
    const char *body = strstr(request, "\r\n\r\n");
    app_modbus_rtu_config_t config;
    app_device_config_t persistent_config;
    uint32_t unit_id;
    uint32_t red_led_on;
    uint32_t buzzer_on;

    if (body != NULL)
    {
        body += 4;
    }
    if ((body == NULL) ||
        (app_netx_json_get_uint(body,
                               "modbus_unit_id",
                               &unit_id) == 0U) ||
        (app_netx_json_get_uint(body, "red_led_on", &red_led_on) == 0U) ||
        (app_netx_json_get_uint(body, "buzzer_on", &buzzer_on) == 0U) ||
        (unit_id == 0UL) || (unit_id > 247UL) ||
        (red_led_on > 1UL) || (buzzer_on > 1UL))
    {
        (void)app_netx_http_send("400 Bad Request",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"invalid configuration\"}\r\n");
        return;
    }

    config.unit_id = (uint8_t)unit_id;
    config.red_led_on = (uint8_t)red_led_on;
    config.buzzer_on = (uint8_t)buzzer_on;
    if (app_modbus_rtu_request_config(&config) != HAL_OK)
    {
        (void)app_netx_http_send("503 Service Unavailable",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"configuration owner unavailable\"}\r\n");
        return;
    }

    persistent_config.modbus_unit_id = config.unit_id;
    if (app_device_config_save(&persistent_config) != HAL_OK)
    {
        (void)app_netx_http_send("500 Internal Server Error",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"applied\":true,\"saved\":false,"
                                 "\"error\":\"W25Q128 save failed\"}\r\n");
        return;
    }

    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"applied\":true,\"saved\":true}\r\n");
}

static uint8_t app_netx_json_get_uint(const char *json,
                                      const char *key,
                                      uint32_t *value)
{
    char pattern[48];
    const char *position;
    char *end;
    unsigned long parsed;

    if ((json == NULL) || (key == NULL) || (value == NULL) ||
        (snprintf(pattern, sizeof(pattern), "\"%s\"", key) <= 0))
    {
        return 0U;
    }

    position = strstr(json, pattern);
    if (position == NULL)
    {
        return 0U;
    }
    position += strlen(pattern);
    while ((*position == ' ') || (*position == '\t'))
    {
        position++;
    }
    if (*position != ':')
    {
        return 0U;
    }
    position++;
    while ((*position == ' ') || (*position == '\t'))
    {
        position++;
    }

    parsed = strtoul(position, &end, 10);
    if ((end == position) || (parsed > 0xFFFFFFFFUL))
    {
        return 0U;
    }
    *value = (uint32_t)parsed;
    return 1U;
}

static UINT app_netx_http_send(const char *status,
                               const char *content_type,
                               const char *body)
{
    NX_PACKET *packet;
    char header[APP_NETX_HTTP_HEADER_SIZE];
    size_t body_length = strlen(body);
    int header_length;
    UINT result;

    header_length = snprintf(header,
                             sizeof(header),
                             "HTTP/1.1 %s\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %lu\r\n"
                             "Connection: close\r\n"
                             "Cache-Control: no-store\r\n\r\n",
                             status,
                             content_type,
                             (unsigned long)body_length);
    if ((header_length <= 0) || ((size_t)header_length >= sizeof(header)))
    {
        return NX_SIZE_ERROR;
    }

    result = nx_packet_allocate(netx_service_packet_pool,
                                &packet,
                                NX_TCP_PACKET,
                                NX_IP_PERIODIC_RATE);
    if (result != NX_SUCCESS)
    {
        return result;
    }

    result = nx_packet_data_append(packet,
                                   header,
                                   (ULONG)header_length,
                                   netx_service_packet_pool,
                                   NX_IP_PERIODIC_RATE);
    if (result == NX_SUCCESS)
    {
        result = nx_packet_data_append(packet,
                                       (void *)body,
                                       (ULONG)body_length,
                                       netx_service_packet_pool,
                                       NX_IP_PERIODIC_RATE);
    }
    if (result == NX_SUCCESS)
    {
        result = nx_tcp_socket_send(&netx_http_socket,
                                    packet,
                                    NX_IP_PERIODIC_RATE);
    }
    if ((result != NX_SUCCESS) && (packet != NX_NULL))
    {
        nx_packet_release(packet);
    }
    return result;
}

static void app_netx_http_build_status(char *body, size_t body_size)
{
    bsp_eth_diagnostics_t diagnostics;
    app_modbus_rtu_diagnostics_t modbus_diagnostics = {0};
    ULONG ip_address = 0UL;
    ULONG network_mask = 0UL;
    ULONG uptime_seconds = tx_time_get() / TX_TIMER_TICKS_PER_SECOND;
    char ip_text[16];

    (void)nx_ip_address_get(netx_service_ip,
                            &ip_address,
                            &network_mask);
    bsp_eth_get_diagnostics(&diagnostics);
    (void)app_modbus_rtu_get_diagnostics(&modbus_diagnostics);
    app_netx_format_ipv4(ip_address, ip_text, sizeof(ip_text));

    (void)snprintf(body,
                   body_size,
                   "{\"board\":\"ART-Pi STM32H750\","
                   "\"ip\":\"%s\","
                   "\"uptime_s\":%lu,"
                   "\"tcp_echo_port\":%u,"
                   "\"http_port\":%u,"
                   "\"eth_rx_frames\":%lu,"
                   "\"eth_tx_frames\":%lu,"
                   "\"eth_rx_drops\":%lu,"
                   "\"eth_rx_errors\":%lu,"
                   "\"eth_tx_errors\":%lu,"
                   "\"modbus_rx_bytes\":%lu,"
                   "\"modbus_rx_frames\":%lu,"
                   "\"modbus_replies\":%lu,"
                   "\"modbus_crc_errors\":%lu,"
                   "\"ldc_overflow\":%lu,"
                   "\"ldc_drop\":%lu}\r\n",
                   ip_text,
                   (unsigned long)uptime_seconds,
                   (unsigned int)APP_NETX_TCP_ECHO_PORT,
                   (unsigned int)APP_NETX_HTTP_PORT,
                   (unsigned long)diagnostics.received_frames,
                   (unsigned long)diagnostics.transmitted_frames,
                   (unsigned long)diagnostics.receive_drops,
                   (unsigned long)diagnostics.receive_errors,
                   (unsigned long)diagnostics.transmit_errors,
                   (unsigned long)modbus_diagnostics.received_bytes,
                   (unsigned long)modbus_diagnostics.received_frames,
                   (unsigned long)modbus_diagnostics.replied_frames,
                   (unsigned long)modbus_diagnostics.crc_errors,
                   (unsigned long)modbus_diagnostics.ldc_overflow,
                   (unsigned long)modbus_diagnostics.ldc_drop);
}

static void app_netx_format_ipv4(ULONG address, char *text, size_t text_size)
{
    (void)snprintf(text,
                   text_size,
                   "%lu.%lu.%lu.%lu",
                   (unsigned long)((address >> 24) & 0xFFUL),
                   (unsigned long)((address >> 16) & 0xFFUL),
                   (unsigned long)((address >> 8) & 0xFFUL),
                   (unsigned long)(address & 0xFFUL));
}
