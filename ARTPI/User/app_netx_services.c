#include "includes.h"
#include "app_device_config.h"
#include "app_gateway_ota.h"
#include "app_h563_ota_cache.h"
#include "app_modbus_rtu.h"
#include "app_netx_services.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define APP_NETX_ECHO_STACK_SIZE        2048U
#define APP_NETX_HTTP_STACK_SIZE        6144U
#define APP_NETX_SERVICE_PRIORITY          5U
#define APP_NETX_LISTEN_QUEUE_SIZE         2U
#define APP_NETX_TCP_WINDOW_SIZE        2048U
#define APP_NETX_REQUEST_BUFFER_SIZE    8192U
#define APP_NETX_RESPONSE_BUFFER_SIZE   8192U
#define APP_NETX_HTTP_HEADER_SIZE        192U
#define APP_NETX_API_VERSION                2U
#define APP_NETX_FIRMWARE_VERSION       "0.2.0"

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

__attribute__((section(".bss.http"), aligned(32)))
static char netx_http_request[APP_NETX_REQUEST_BUFFER_SIZE];

__attribute__((section(".bss.http"), aligned(32)))
static char netx_http_response[APP_NETX_RESPONSE_BUFFER_SIZE];

__attribute__((section(".bss.http"), aligned(32)))
static app_modbus_rtu_diagnostics_t netx_modbus_diagnostics;

static const char netx_config_page[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ART-Pi RS485 Gateway</title><style>"
    "*{box-sizing:border-box}body{font-family:system-ui,sans-serif;background:#f3f6fb;"
    "color:#182230;margin:0;padding:20px}main{max-width:1180px;margin:auto}"
    ".card,.dev{background:#fff;border:1px solid #e3e8ef;border-radius:14px;"
    "padding:18px;margin:12px 0;box-shadow:0 5px 20px #2030500d}"
    "h1{margin:0 0 4px}h2{font-size:19px;margin:0 0 12px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}"
    ".ranges{display:grid;grid-template-columns:repeat(4,minmax(155px,1fr));gap:8px}"
    "label{display:flex;flex-direction:column;gap:5px;font-size:13px;color:#52606d}"
    "input,select,button{padding:9px;border:1px solid #b8c2d1;border-radius:8px;"
    "font:inherit}button{background:#1769e0;color:#fff;border:0;cursor:pointer}"
    "button.alt{background:#344054}.actions{display:flex;gap:10px;flex-wrap:wrap}"
    ".actions button{min-width:180px}.note{color:#667085;font-size:13px}"
    ".ok{color:#087443}.bad{color:#b42318}.hide{display:none}"
    "pre{background:#101828;color:#d0f0dc;padding:14px;border-radius:10px;"
    "overflow:auto;max-height:360px}.dev h3{margin:0 0 10px}"
    "@media(max-width:760px){.ranges{grid-template-columns:1fr 1fr}}"
    "</style></head><body><main>"
    "<h1>ART-Pi STM32H750 RS485 Gateway</h1>"
    "<p class='note'>ThreadX + LDC + ld_modbus + NetX Duo</p>"
    "<section class='card'><h2>RS485 role and local outputs</h2><div class='grid'>"
    "<label>Role<select id='role' onchange='roleChanged()'>"
    "<option value='0'>Slave</option><option value='1'>Master</option></select></label>"
    "<label>Slave unit ID<input id='slaveUnit' type='number' min='1' max='247'></label>"
    "<label>Master device count<input id='deviceCount' type='number' min='1' max='10'"
    " onchange='deviceCountChanged()'></label>"
    "<label>Long offline probe<select id='offlineProbe'>"
    "<option value='60'>60 seconds</option><option value='300'>5 minutes</option>"
    "</select></label><label>Red LED<input id='red' type='checkbox'></label>"
    "<label>Buzzer<input id='buzzer' type='checkbox'></label></div></section>"
    "<section id='masterSection'><div id='devices'></div>"
    "<section class='card'><h2>High-priority write command</h2><div class='grid'>"
    "<label>Device<select id='cmdDevice'></select></label>"
    "<label>Command<select id='cmdType'><option value='0'>FC05 coil</option>"
    "<option value='1'>FC06 holding register</option></select></label>"
    "<label>Address<input id='cmdAddress' type='number' min='0' max='65535' value='0'></label>"
    "<label>Value<input id='cmdValue' type='number' min='0' max='65535' value='1'></label>"
    "</div><button onclick='sendCommand()'>Queue priority command</button></section>"
    "</section><section class='card'><div class='actions'>"
    "<button id='save' onclick='saveConfig()'>Save and apply configuration</button>"
    "<button class='alt' onclick='refreshStatus()'>Refresh status</button></div>"
    "<p id='message'></p><pre id='status'>status not loaded</pre></section>"
    "</main><script>"
    "const classes=[['coil','Coils'],['discrete','Discrete inputs'],"
    "['holding','Holding registers'],['input','Input registers']];"
    "const $=id=>document.getElementById(id);"
    "function deviceCard(i){let ranges='';for(const [k,n] of classes){"
    "ranges+=`<div><strong>${n}</strong><label>Start address<input id='d${i}_${k}_addr'"
    " type='number' min='0' max='65535'></label><label>Quantity (0 disables)"
    "<input id='d${i}_${k}_qty' type='number' min='0' max='16'></label></div>`;}"
    "return `<section class='dev' id='dev${i}'><h3>Device ${i+1}</h3>"
    "<div class='grid'><label>Unit ID<input id='d${i}_unit' type='number' min='1'"
    " max='247'></label><label>Response timeout ms<input id='d${i}_timeout'"
    " type='number' min='20' max='5000'></label></div><div class='ranges'>"
    "${ranges}</div></section>`;}"
    "function buildDevices(){let h='';for(let i=0;i<10;i++)h+=deviceCard(i);"
    "$('devices').innerHTML=h;}"
    "function deviceCountChanged(){const n=Number($('deviceCount').value)||1;"
    "let opts='';for(let i=0;i<10;i++){$('dev'+i).classList.toggle('hide',i>=n);"
    "if(i<n)opts+=`<option value='${i}'>Device ${i+1}</option>`;}"
    "$('cmdDevice').innerHTML=opts;}"
    "function roleChanged(){$('masterSection').classList.toggle('hide',$('role').value!='1');}"
    "function show(t,ok){$('message').textContent=t;$('message').className=ok?'ok':'bad';}"
    "async function refresh(){try{const r=await fetch('/api/config',{cache:'no-store'});"
    "if(!r.ok)throw new Error('HTTP '+r.status);const c=await r.json();"
    "$('role').value=c.rs485_role;$('slaveUnit').value=c.modbus_unit_id;"
    "$('deviceCount').value=c.master_device_count;$('offlineProbe').value=c.offline_probe_s;"
    "$('red').checked=!!c.red_led_on;$('buzzer').checked=!!c.buzzer_on;"
    "for(let i=0;i<10;i++){const d=c.devices[i];$('d'+i+'_unit').value=d.unit_id;"
    "$('d'+i+'_timeout').value=d.timeout_ms;for(const [k] of classes){"
    "$('d'+i+'_'+k+'_addr').value=d[k].address;"
    "$('d'+i+'_'+k+'_qty').value=d[k].quantity;}}"
    "deviceCountChanged();roleChanged();await refreshStatus();}"
    "catch(e){show(e.message,false);}}"
    "function num(id){return Number($(id).value);}"
    "async function saveConfig(){const c={rs485_role:num('role'),"
    "modbus_unit_id:num('slaveUnit'),master_device_count:num('deviceCount'),"
    "offline_probe_s:num('offlineProbe'),red_led_on:$('red').checked?1:0,"
    "buzzer_on:$('buzzer').checked?1:0};for(let i=0;i<10;i++){"
    "c['d'+i+'_unit_id']=num('d'+i+'_unit');c['d'+i+'_timeout_ms']=num('d'+i+'_timeout');"
    "for(const [k] of classes){c['d'+i+'_'+k+'_address']=num('d'+i+'_'+k+'_addr');"
    "c['d'+i+'_'+k+'_quantity']=num('d'+i+'_'+k+'_qty');}}"
    "$('save').disabled=true;try{const r=await fetch('/api/config',{method:'POST',"
    "headers:{'Content-Type':'application/json'},body:JSON.stringify(c)});"
    "const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.error||('HTTP '+r.status));"
    "show('Configuration saved and queued for the RS485 owner task',true);"
    "setTimeout(refresh,250);}catch(e){show(e.message,false);}finally{$('save').disabled=false;}}"
    "async function sendCommand(){const body={device_index:num('cmdDevice'),"
    "type:num('cmdType'),address:num('cmdAddress'),value:num('cmdValue')};"
    "try{const r=await fetch('/api/rs485/command',{method:'POST',headers:"
    "{'Content-Type':'application/json'},body:JSON.stringify(body)});const j=await r.json();"
    "if(!r.ok||!j.ok)throw new Error(j.error||('HTTP '+r.status));"
    "show('Priority command queued, id='+j.command_id,true);setTimeout(refreshStatus,250);}"
    "catch(e){show(e.message,false);}}"
    "async function refreshStatus(){try{const r=await fetch('/api/rs485/status',"
    "{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);"
    "$('status').textContent=JSON.stringify(await r.json(),null,2);}"
    "catch(e){$('status').textContent=e.message;}}"
    "buildDevices();refresh();setInterval(refreshStatus,2000);"
    "</script></body></html>";

static void app_netx_echo_thread_entry(ULONG input);
static void app_netx_http_thread_entry(ULONG input);
static void app_netx_recycle_server_socket(NX_TCP_SOCKET *socket, UINT port);
static void app_netx_relisten(NX_TCP_SOCKET *socket, UINT port);
static UINT app_netx_http_receive_request(char *request,
                                          size_t request_size,
                                          size_t *request_length);
static uint8_t app_netx_ascii_equal_ignore_case(const char *left,
                                                const char *right,
                                                size_t length);
static size_t app_netx_http_content_length(const char *request);
static void app_netx_http_process(const char *request, size_t request_length);
static void app_netx_http_send_config(void);
static void app_netx_http_update_config(const char *request);
static void app_netx_http_send_config_error(const char *field,
                                             const char *reason);
static void app_netx_http_queue_command(const char *request);
static void app_netx_http_h563_ota_manifest(const char *request,
                                             size_t request_length);
static void app_netx_http_h563_ota_chunk(const char *request,
                                          size_t request_length);
static void app_netx_http_h563_ota_finish(void);
static void app_netx_http_h563_ota_start(const char *request);
static void app_netx_http_h563_ota_status(void);
static void app_netx_http_gateway_ota_manifest(const char *request,
                                                size_t request_length);
static void app_netx_http_gateway_ota_chunk(const char *request,
                                             size_t request_length);
static void app_netx_http_gateway_ota_finish(void);
static void app_netx_http_gateway_ota_start(void);
static void app_netx_http_gateway_ota_status(void);
static uint8_t app_netx_http_get_body(const char *request,
                                      size_t request_length,
                                      const uint8_t **body,
                                      size_t *body_length);
static uint8_t app_netx_json_get_uint(const char *json,
                                      const char *key,
                                      uint32_t *value);
static UINT app_netx_http_send(const char *status,
                               const char *content_type,
                               const char *body);
static void app_netx_http_build_status(char *body, size_t body_size);
static uint8_t app_netx_http_append(char *body,
                                    size_t body_size,
                                    size_t *used,
                                    const char *format,
                                    ...);
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

        app_netx_recycle_server_socket(&netx_echo_socket,
                                       APP_NETX_TCP_ECHO_PORT);
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
            size_t request_length;

            status = app_netx_http_receive_request(netx_http_request,
                                                   sizeof(netx_http_request),
                                                   &request_length);
            if (status == NX_SUCCESS)
            {
                app_netx_http_process(netx_http_request, request_length);
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

        app_netx_recycle_server_socket(&netx_http_socket,
                                       APP_NETX_HTTP_PORT);
    }
}

static void app_netx_recycle_server_socket(NX_TCP_SOCKET *socket, UINT port)
{
    UINT status;

    status = nx_tcp_socket_disconnect(socket, NX_IP_PERIODIC_RATE);
    if ((status != NX_SUCCESS) && (status != NX_NOT_CONNECTED))
    {
        (void)nx_tcp_socket_disconnect(socket, NX_NO_WAIT);
    }

    status = nx_tcp_server_socket_unaccept(socket);
    if (status != NX_SUCCESS)
    {
        /* A peer can disappear while a reply is being sent.  Force NetX back
         * to a reusable server-socket state instead of leaving this thread in
         * an endless NX_NOT_CLOSED relisten loop. */
        (void)nx_tcp_socket_disconnect(socket, NX_NO_WAIT);
        (void)nx_tcp_server_socket_unaccept(socket);
    }

    app_netx_relisten(socket, port);
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

        (void)nx_tcp_socket_disconnect(socket, NX_NO_WAIT);
        (void)nx_tcp_server_socket_unaccept(socket);
        tx_thread_sleep(1U);
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

static uint8_t app_netx_ascii_equal_ignore_case(const char *left,
                                                const char *right,
                                                size_t length)
{
    size_t index;

    for (index = 0U; index < length; index++)
    {
        char left_character = left[index];
        char right_character = right[index];

        if ((left_character == '\0') || (right_character == '\0'))
        {
            return 0U;
        }
        if ((left_character >= 'A') && (left_character <= 'Z'))
        {
            left_character = (char)(left_character + ('a' - 'A'));
        }
        if ((right_character >= 'A') && (right_character <= 'Z'))
        {
            right_character = (char)(right_character + ('a' - 'A'));
        }
        if (left_character != right_character)
        {
            return 0U;
        }
    }

    return 1U;
}

static size_t app_netx_http_content_length(const char *request)
{
    static const char field_name[] = "content-length:";
    const char *field = request;
    char *end;
    unsigned long length;

    while ((*field != '\0') &&
           (app_netx_ascii_equal_ignore_case(field,
                                             field_name,
                                             sizeof(field_name) - 1U) == 0U))
    {
        field++;
    }
    if (*field == '\0')
    {
        return 0U;
    }

    field += sizeof(field_name) - 1U;
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
    if ((strncmp(request, "GET /status ", 12U) == 0) ||
        (strncmp(request, "GET /api/status ", 16U) == 0) ||
        (strncmp(request, "GET /api/rs485/status ", 22U) == 0))
    {
        app_netx_http_build_status(netx_http_response,
                                   sizeof(netx_http_response));
        (void)app_netx_http_send("200 OK",
                                 "application/json; charset=utf-8",
                                 netx_http_response);
    }
    else if (strncmp(request, "GET /api/config ", 16U) == 0)
    {
        app_netx_http_send_config();
    }
    else if (strncmp(request, "POST /api/config ", 17U) == 0)
    {
        app_netx_http_update_config(request);
    }
    else if (strncmp(request, "POST /api/rs485/command ", 24U) == 0)
    {
        app_netx_http_queue_command(request);
    }
    else if (strncmp(request, "POST /api/ota/h563/manifest ", 28U) == 0)
    {
        app_netx_http_h563_ota_manifest(request, request_length);
    }
    else if (strncmp(request, "POST /api/ota/h563/chunk?offset=", 32U) == 0)
    {
        app_netx_http_h563_ota_chunk(request, request_length);
    }
    else if (strncmp(request, "POST /api/ota/h563/finish ", 26U) == 0)
    {
        app_netx_http_h563_ota_finish();
    }
    else if (strncmp(request, "POST /api/ota/h563/start ", 25U) == 0)
    {
        app_netx_http_h563_ota_start(request);
    }
    else if (strncmp(request, "GET /api/ota/h563/status ", 25U) == 0)
    {
        app_netx_http_h563_ota_status();
    }
    else if (strncmp(request,
                     "POST /api/ota/gateway/manifest ",
                     31U) == 0)
    {
        app_netx_http_gateway_ota_manifest(request, request_length);
    }
    else if (strncmp(request,
                     "POST /api/ota/gateway/chunk?offset=",
                     35U) == 0)
    {
        app_netx_http_gateway_ota_chunk(request, request_length);
    }
    else if (strncmp(request,
                     "POST /api/ota/gateway/finish ",
                     29U) == 0)
    {
        app_netx_http_gateway_ota_finish();
    }
    else if (strncmp(request,
                     "POST /api/ota/gateway/start ",
                     28U) == 0)
    {
        app_netx_http_gateway_ota_start();
    }
    else if (strncmp(request,
                     "GET /api/ota/gateway/status ",
                     28U) == 0)
    {
        app_netx_http_gateway_ota_status();
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
    size_t used = 0U;
    uint32_t device_index;

    if (app_modbus_rtu_get_config(&config) != HAL_OK)
    {
        (void)app_netx_http_send("503 Service Unavailable",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"Modbus service unavailable\"}\r\n");
        return;
    }

    if (app_netx_http_append(netx_http_response,
                             sizeof(netx_http_response),
                             &used,
                             "{\"ok\":true,\"api_version\":%u,"
                             "\"firmware_version\":\"%s\",\"rs485_role\":%u,"
                             "\"modbus_unit_id\":%u,"
                             "\"master_device_count\":%u,"
                             "\"offline_probe_s\":%u,"
                             "\"poll_period_ms\":%u,"
                             "\"red_led_on\":%u,\"buzzer_on\":%u,"
                             "\"devices\":[",
                             (unsigned int)APP_NETX_API_VERSION,
                             APP_NETX_FIRMWARE_VERSION,
                             (unsigned int)config.persistent.rs485_role,
                             (unsigned int)config.persistent.modbus_unit_id,
                             (unsigned int)config.persistent.master_device_count,
                             (unsigned int)config.persistent.offline_probe_period_s,
                             (unsigned int)config.persistent.poll_period_ms,
                             (unsigned int)config.red_led_on,
                             (unsigned int)config.buzzer_on) == 0U)
    {
        (void)app_netx_http_send("500 Internal Server Error",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"response too large\"}\r\n");
        return;
    }

    for (device_index = 0U;
         device_index < APP_DEVICE_CONFIG_MAX_MASTER_DEVICES;
         device_index++)
    {
        const app_modbus_master_device_config_t *device =
            &config.persistent.master_devices[device_index];

        if (app_netx_http_append(netx_http_response,
                                 sizeof(netx_http_response),
                                 &used,
                                 "%s{\"unit_id\":%u,\"timeout_ms\":%u,"
                                 "\"coil\":{\"address\":%u,\"quantity\":%u},"
                                 "\"discrete\":{\"address\":%u,\"quantity\":%u},"
                                 "\"holding\":{\"address\":%u,\"quantity\":%u},"
                                 "\"input\":{\"address\":%u,\"quantity\":%u}}",
                                 (device_index == 0U) ? "" : ",",
                                 (unsigned int)device->unit_id,
                                 (unsigned int)device->response_timeout_ms,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_COILS].address,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_COILS].quantity,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_DISCRETE_INPUTS].address,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_DISCRETE_INPUTS].quantity,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_HOLDING_REGISTERS].address,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_HOLDING_REGISTERS].quantity,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_INPUT_REGISTERS].address,
                                 (unsigned int)device->ranges[APP_MODBUS_REGISTER_INPUT_REGISTERS].quantity) == 0U)
        {
            (void)app_netx_http_send("500 Internal Server Error",
                                     "application/json; charset=utf-8",
                                     "{\"ok\":false,\"error\":\"response too large\"}\r\n");
            return;
        }
    }

    (void)app_netx_http_append(netx_http_response,
                               sizeof(netx_http_response),
                               &used,
                               "]}\r\n");
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             netx_http_response);
}

static void app_netx_http_update_config(const char *request)
{
    const char *body = strstr(request, "\r\n\r\n");
    const char *invalid_field = "request";
    const char *invalid_reason = "missing or out of range";
    app_modbus_rtu_config_t config;
    uint32_t value;
    uint32_t device_index;
    uint32_t range_index;
    char key[48];
    static const char *range_names[APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT] =
    {
        "coil", "discrete", "holding", "input"
    };

    if (body != NULL)
    {
        body += 4;
    }
    if (body == NULL)
    {
        app_netx_http_send_config_error("request", "missing JSON body");
        return;
    }
    if (app_modbus_rtu_get_config(&config) != HAL_OK)
    {
        (void)app_netx_http_send("503 Service Unavailable",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"configuration owner unavailable\"}\r\n");
        return;
    }
    if ((app_netx_json_get_uint(body, "rs485_role", &value) == 0U) ||
        (value > APP_RS485_ROLE_MASTER))
    {
        app_netx_http_send_config_error("rs485_role", "expected 0 or 1");
        return;
    }

    config.persistent.rs485_role = (uint8_t)value;
    if ((app_netx_json_get_uint(body, "modbus_unit_id", &value) == 0U) ||
        (value == 0UL) || (value > 247UL))
    {
        invalid_field = "modbus_unit_id";
        invalid_reason = "expected 1..247";
        goto invalid_configuration;
    }
    config.persistent.modbus_unit_id = (uint8_t)value;
    if ((app_netx_json_get_uint(body, "master_device_count", &value) == 0U) ||
        (value == 0UL) ||
        (value > APP_DEVICE_CONFIG_MAX_MASTER_DEVICES))
    {
        invalid_field = "master_device_count";
        invalid_reason = "expected 1..10";
        goto invalid_configuration;
    }
    config.persistent.master_device_count = (uint8_t)value;
    if ((app_netx_json_get_uint(body, "offline_probe_s", &value) == 0U) ||
        ((value != 60UL) && (value != 300UL)))
    {
        invalid_field = "offline_probe_s";
        invalid_reason = "expected 60 or 300";
        goto invalid_configuration;
    }
    config.persistent.offline_probe_period_s = (uint16_t)value;
    if ((app_netx_json_get_uint(body, "poll_period_ms", &value) == 0U) ||
        (value < APP_DEVICE_CONFIG_MIN_POLL_PERIOD_MS) ||
        (value > APP_DEVICE_CONFIG_MAX_POLL_PERIOD_MS))
    {
        invalid_field = "poll_period_ms";
        invalid_reason = "expected 100..60000";
        goto invalid_configuration;
    }
    config.persistent.poll_period_ms = (uint16_t)value;
    if ((app_netx_json_get_uint(body, "red_led_on", &value) == 0U) ||
        (value > 1UL))
    {
        invalid_field = "red_led_on";
        invalid_reason = "expected 0 or 1";
        goto invalid_configuration;
    }
    config.red_led_on = (uint8_t)value;
    if ((app_netx_json_get_uint(body, "buzzer_on", &value) == 0U) ||
        (value > 1UL))
    {
        invalid_field = "buzzer_on";
        invalid_reason = "expected 0 or 1";
        goto invalid_configuration;
    }
    config.buzzer_on = (uint8_t)value;

    for (device_index = 0U;
         device_index < config.persistent.master_device_count;
         device_index++)
    {
        app_modbus_master_device_config_t *device =
            &config.persistent.master_devices[device_index];

        (void)snprintf(key, sizeof(key), "d%lu_unit_id",
                       (unsigned long)device_index);
        if ((app_netx_json_get_uint(body, key, &value) == 0U) ||
            (value == 0UL) || (value > 247UL))
        {
            invalid_field = key;
            invalid_reason = "expected 1..247";
            goto invalid_configuration;
        }
        device->unit_id = (uint8_t)value;

        (void)snprintf(key, sizeof(key), "d%lu_timeout_ms",
                       (unsigned long)device_index);
        if ((app_netx_json_get_uint(body, key, &value) == 0U) ||
            (value < APP_DEVICE_CONFIG_MIN_RESPONSE_TIMEOUT_MS) ||
            (value > APP_DEVICE_CONFIG_MAX_RESPONSE_TIMEOUT_MS))
        {
            invalid_field = key;
            invalid_reason = "expected 20..5000";
            goto invalid_configuration;
        }
        device->response_timeout_ms = (uint16_t)value;

        for (range_index = 0U;
             range_index < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
             range_index++)
        {
            (void)snprintf(key,
                           sizeof(key),
                           "d%lu_%s_address",
                           (unsigned long)device_index,
                           range_names[range_index]);
            if ((app_netx_json_get_uint(body, key, &value) == 0U) ||
                (value > 65535UL))
            {
                invalid_field = key;
                invalid_reason = "expected 0..65535";
                goto invalid_configuration;
            }
            device->ranges[range_index].address = (uint16_t)value;

            (void)snprintf(key,
                           sizeof(key),
                           "d%lu_%s_quantity",
                           (unsigned long)device_index,
                           range_names[range_index]);
            if ((app_netx_json_get_uint(body, key, &value) == 0U) ||
                (value > APP_DEVICE_CONFIG_MAX_RANGE_QUANTITY))
            {
                invalid_field = key;
                invalid_reason = "expected 0..16";
                goto invalid_configuration;
            }
            device->ranges[range_index].quantity = (uint16_t)value;
        }
    }

    if (app_device_config_validate(&config.persistent) == 0U)
    {
        invalid_field = "configuration";
        invalid_reason = "duplicate unit, empty ranges, or address overflow";
        goto invalid_configuration;
    }

    if (app_modbus_rtu_request_config(&config) != HAL_OK)
    {
        (void)app_netx_http_send("503 Service Unavailable",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"configuration owner unavailable\"}\r\n");
        return;
    }

    if (app_device_config_save(&config.persistent) != HAL_OK)
    {
        (void)app_netx_http_send("500 Internal Server Error",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"applied\":true,\"saved\":false,"
                                 "\"error\":\"W25Q128 save failed\"}\r\n");
        return;
    }

    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"api_version\":2,\"applied\":true,\"saved\":true}\r\n");
    return;

invalid_configuration:
    app_netx_http_send_config_error(invalid_field, invalid_reason);
}

static void app_netx_http_send_config_error(const char *field,
                                             const char *reason)
{
    (void)snprintf(netx_http_response,
                   sizeof(netx_http_response),
                   "{\"ok\":false,\"error\":\"invalid configuration\","
                   "\"field\":\"%s\",\"reason\":\"%s\"}\r\n",
                   (field != NULL) ? field : "configuration",
                   (reason != NULL) ? reason : "invalid value");
    (void)app_netx_http_send("400 Bad Request",
                             "application/json; charset=utf-8",
                             netx_http_response);
}

static void app_netx_http_queue_command(const char *request)
{
    const char *body = strstr(request, "\r\n\r\n");
    app_modbus_command_t command;
    uint32_t value;
    uint32_t command_id;

    if (body != NULL)
    {
        body += 4;
    }
    if ((body == NULL) ||
        (app_netx_json_get_uint(body, "device_index", &value) == 0U) ||
        (value >= APP_DEVICE_CONFIG_MAX_MASTER_DEVICES))
    {
        goto invalid_command;
    }
    command.device_index = (uint8_t)value;
    if ((app_netx_json_get_uint(body, "type", &value) == 0U) ||
        (value > APP_MODBUS_COMMAND_WRITE_REGISTER))
    {
        goto invalid_command;
    }
    command.type = (uint8_t)value;
    if ((app_netx_json_get_uint(body, "address", &value) == 0U) ||
        (value > 65535UL))
    {
        goto invalid_command;
    }
    command.address = (uint16_t)value;
    if ((app_netx_json_get_uint(body, "value", &value) == 0U) ||
        (value > 65535UL) ||
        ((command.type == APP_MODBUS_COMMAND_WRITE_COIL) && (value > 1UL)))
    {
        goto invalid_command;
    }
    command.value = (uint16_t)value;

    if (app_modbus_rtu_queue_command(&command, &command_id) != HAL_OK)
    {
        (void)app_netx_http_send("503 Service Unavailable",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"command queue unavailable or full\"}\r\n");
        return;
    }

    (void)snprintf(netx_http_response,
                   sizeof(netx_http_response),
                   "{\"ok\":true,\"command_id\":%lu,\"queued\":true}\r\n",
                   (unsigned long)command_id);
    (void)app_netx_http_send("202 Accepted",
                             "application/json; charset=utf-8",
                             netx_http_response);
    return;

invalid_command:
    (void)app_netx_http_send("400 Bad Request",
                             "application/json; charset=utf-8",
                             "{\"ok\":false,\"error\":\"invalid command\"}\r\n");
}

static uint8_t app_netx_http_get_body(const char *request,
                                      size_t request_length,
                                      const uint8_t **body,
                                      size_t *body_length)
{
    const char *header_end;
    size_t header_length;
    size_t content_length;

    if(request == NULL || body == NULL || body_length == NULL)
        return 0U;
    header_end = strstr(request, "\r\n\r\n");
    if(header_end == NULL)
        return 0U;
    header_length = (size_t)(header_end - request) + 4U;
    content_length = app_netx_http_content_length(request);
    if(header_length > request_length ||
       content_length > (request_length - header_length))
    {
        return 0U;
    }
    *body = (const uint8_t *)&request[header_length];
    *body_length = content_length;
    return 1U;
}

static void app_netx_http_h563_ota_manifest(const char *request,
                                             size_t request_length)
{
    const uint8_t *body;
    size_t body_length;

    if(app_netx_http_get_body(request,
                              request_length,
                              &body,
                              &body_length) == 0U ||
       body_length != APP_H563_OTA_MANIFEST_SIZE)
    {
        (void)app_netx_http_send("400 Bad Request",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"manifest must be 188 bytes\"}\r\n");
        return;
    }
    if(app_h563_ota_cache_write_manifest(body, body_length) != HAL_OK)
    {
        (void)app_netx_http_send("400 Bad Request",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"manifest invalid or cache busy\"}\r\n");
        return;
    }
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"next_offset\":0}\r\n");
}

static void app_netx_http_h563_ota_chunk(const char *request,
                                          size_t request_length)
{
    const char *offset_text = strstr(request, "?offset=");
    const uint8_t *body;
    size_t body_length;
    char *end;
    unsigned long offset;

    if(offset_text == NULL)
        goto invalid_chunk;
    offset_text += 8;
    offset = strtoul(offset_text, &end, 10);
    if(end == offset_text || *end != ' ' || offset > 0xFFFFFFFFUL ||
       app_netx_http_get_body(request,
                              request_length,
                              &body,
                              &body_length) == 0U ||
       body_length == 0U)
    {
        goto invalid_chunk;
    }
    if(app_h563_ota_cache_write_image((uint32_t)offset,
                                       body,
                                       body_length) != HAL_OK)
    {
        (void)app_netx_http_send("409 Conflict",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"chunk rejected; check sequential offset and cache state\"}\r\n");
        return;
    }
    (void)snprintf(netx_http_response,
                   sizeof(netx_http_response),
                   "{\"ok\":true,\"next_offset\":%lu}\r\n",
                   offset + (unsigned long)body_length);
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             netx_http_response);
    return;

invalid_chunk:
    (void)app_netx_http_send("400 Bad Request",
                             "application/json; charset=utf-8",
                             "{\"ok\":false,\"error\":\"invalid OTA chunk\"}\r\n");
}

static void app_netx_http_h563_ota_finish(void)
{
    if(app_h563_ota_cache_finish() != HAL_OK)
    {
        (void)app_netx_http_send("422 Unprocessable Entity",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"image incomplete or CRC mismatch\"}\r\n");
        return;
    }
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"verified\":true}\r\n");
}

static void app_netx_http_h563_ota_start(const char *request)
{
    const char *body = strstr(request, "\r\n\r\n");
    uint32_t unit_id;
    uint32_t baud_rate = 0U;
    HAL_StatusTypeDef status;

    if(body != NULL)
        body += 4;
    if(body == NULL ||
       app_netx_json_get_uint(body, "unit_id", &unit_id) == 0U ||
       unit_id == 0U || unit_id > 247U)
    {
        (void)app_netx_http_send("400 Bad Request",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"unit_id must be 1..247\"}\r\n");
        return;
    }
    (void)app_netx_json_get_uint(body, "baud_rate", &baud_rate);
    status = app_modbus_rtu_request_h563_ota((uint8_t)unit_id, baud_rate);
    if(status != HAL_OK)
    {
        (void)app_netx_http_send((status == HAL_BUSY) ?
                                     "409 Conflict" :
                                     "422 Unprocessable Entity",
                                 "application/json; charset=utf-8",
                                 "{\"ok\":false,\"error\":\"OTA cache not ready or transfer busy\"}\r\n");
        return;
    }
    (void)app_netx_http_send("202 Accepted",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"queued\":true}\r\n");
}

static void app_netx_http_h563_ota_status(void)
{
    app_h563_ota_cache_status_t cache;
    app_h563_ota_master_status_t transfer;

    app_h563_ota_cache_get_status(&cache);
    app_modbus_rtu_get_h563_ota_status(&transfer);
    (void)snprintf(netx_http_response,
                   sizeof(netx_http_response),
                   "{\"ok\":true,\"cache\":{\"state\":\"%s\","
                   "\"state_id\":%u,\"error\":%u,\"received\":%lu,"
                   "\"expected\":%lu,\"version\":%lu,\"crc32\":%lu},"
                   "\"transfer\":{\"phase\":\"%s\",\"phase_id\":%u,"
                   "\"result\":\"%s\",\"result_id\":%u,\"unit_id\":%u,"
                   "\"remote_status\":%u,\"session_id\":%lu,"
                   "\"requested_baud\":%lu,\"active_baud\":%lu,"
                   "\"transferred\":%lu,\"total\":%lu,\"retries\":%lu,"
                   "\"started_ms\":%lu,\"finished_ms\":%lu}}\r\n",
                   app_h563_ota_cache_state_name(cache.state),
                   (unsigned int)cache.state,
                   (unsigned int)cache.last_error,
                   (unsigned long)cache.received_size,
                   (unsigned long)cache.expected_size,
                   (unsigned long)cache.image_version,
                   (unsigned long)cache.image_crc32,
                   app_h563_ota_phase_name(transfer.phase),
                   (unsigned int)transfer.phase,
                   app_h563_ota_result_name(transfer.result),
                   (unsigned int)transfer.result,
                   (unsigned int)transfer.unit_id,
                   (unsigned int)transfer.last_remote_status,
                   (unsigned long)transfer.session_id,
                   (unsigned long)transfer.requested_baud_rate,
                   (unsigned long)transfer.active_baud_rate,
                   (unsigned long)transfer.transferred_size,
                   (unsigned long)transfer.image_size,
                   (unsigned long)transfer.retry_count,
                   (unsigned long)transfer.started_ms,
                   (unsigned long)transfer.finished_ms);
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             netx_http_response);
}

static void app_netx_http_gateway_ota_manifest(const char *request,
                                                size_t request_length)
{
    const uint8_t *body;
    size_t body_length;

    if(app_netx_http_get_body(request,
                              request_length,
                              &body,
                              &body_length) == 0U ||
       body_length != 188U)
    {
        (void)app_netx_http_send(
            "400 Bad Request",
            "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"manifest must be 188 bytes\"}\r\n");
        return;
    }
    if(app_gateway_ota_write_manifest(body, body_length) != HAL_OK)
    {
        (void)app_netx_http_send(
            "400 Bad Request",
            "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"gateway manifest invalid or staging unavailable\"}\r\n");
        return;
    }
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"next_offset\":0}\r\n");
}

static void app_netx_http_gateway_ota_chunk(const char *request,
                                             size_t request_length)
{
    const char *offset_text = strstr(request, "?offset=");
    const uint8_t *body;
    size_t body_length;
    char *end;
    unsigned long offset;

    if(offset_text == NULL)
    {
        goto invalid_chunk;
    }
    offset_text += 8;
    offset = strtoul(offset_text, &end, 10);
    if(end == offset_text || *end != ' ' || offset > 0xFFFFFFFFUL ||
       app_netx_http_get_body(request,
                              request_length,
                              &body,
                              &body_length) == 0U ||
       body_length == 0U)
    {
        goto invalid_chunk;
    }
    if(app_gateway_ota_write_image((uint32_t)offset,
                                    body,
                                    body_length) != HAL_OK)
    {
        (void)app_netx_http_send(
            "409 Conflict",
            "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"gateway chunk rejected; use the returned sequential offset\"}\r\n");
        return;
    }
    (void)snprintf(netx_http_response,
                   sizeof(netx_http_response),
                   "{\"ok\":true,\"next_offset\":%lu}\r\n",
                   offset + (unsigned long)body_length);
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             netx_http_response);
    return;

invalid_chunk:
    (void)app_netx_http_send("400 Bad Request",
                             "application/json; charset=utf-8",
                             "{\"ok\":false,\"error\":\"invalid gateway OTA chunk\"}\r\n");
}

static void app_netx_http_gateway_ota_finish(void)
{
    if(app_gateway_ota_finish() != HAL_OK)
    {
        (void)app_netx_http_send(
            "422 Unprocessable Entity",
            "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"gateway image incomplete or CRC/SHA mismatch\"}\r\n");
        return;
    }
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"verified\":true,\"install_requested\":false}\r\n");
}

static void app_netx_http_gateway_ota_start(void)
{
    if(app_gateway_ota_request_install() != HAL_OK)
    {
        (void)app_netx_http_send(
            "409 Conflict",
            "application/json; charset=utf-8",
            "{\"ok\":false,\"error\":\"gateway image is not ready\"}\r\n");
        return;
    }
    (void)app_netx_http_send("202 Accepted",
                             "application/json; charset=utf-8",
                             "{\"ok\":true,\"install_requested\":true,\"rebooting\":true}\r\n");
    (void)tx_thread_sleep(200U);
    NVIC_SystemReset();
}

static void app_netx_http_gateway_ota_status(void)
{
    app_gateway_ota_status_t status;

    app_gateway_ota_get_status(&status);
    (void)snprintf(netx_http_response,
                   sizeof(netx_http_response),
                   "{\"ok\":true,\"state\":\"%s\",\"state_id\":%u,"
                   "\"error\":%u,\"received\":%lu,\"expected\":%lu,"
                   "\"version\":%lu,\"crc32\":%lu,"
                   "\"install_requested\":%u,\"consumed\":%u,"
                   "\"boot_error\":%lu,\"active_version\":%lu,"
                   "\"pending_version\":%lu,\"boot_sequence\":%lu}\r\n",
                   app_gateway_ota_state_name(status.state),
                   (unsigned int)status.state,
                   (unsigned int)status.last_error,
                   (unsigned long)status.received_size,
                   (unsigned long)status.expected_size,
                   (unsigned long)status.image_version,
                   (unsigned long)status.image_crc32,
                   (unsigned int)status.install_requested,
                   (unsigned int)status.consumed,
                   (unsigned long)status.boot_error,
                   (unsigned long)status.active_version,
                   (unsigned long)status.pending_version,
                   (unsigned long)status.boot_sequence);
    (void)app_netx_http_send("200 OK",
                             "application/json; charset=utf-8",
                             netx_http_response);
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
    app_modbus_rtu_config_t config;
    ULONG ip_address = 0UL;
    ULONG network_mask = 0UL;
    ULONG uptime_seconds = tx_time_get() / TX_TIMER_TICKS_PER_SECOND;
    char ip_text[16];
    size_t used = 0U;
    uint32_t device_index;
    uint32_t range_index;
    uint32_t value_index;

    (void)nx_ip_address_get(netx_service_ip,
                            &ip_address,
                            &network_mask);
    bsp_eth_get_diagnostics(&diagnostics);
    memset(&netx_modbus_diagnostics, 0, sizeof(netx_modbus_diagnostics));
    (void)app_modbus_rtu_get_diagnostics(&netx_modbus_diagnostics);
    if (app_modbus_rtu_get_config(&config) != HAL_OK)
    {
        memset(&config, 0, sizeof(config));
    }
    app_netx_format_ipv4(ip_address, ip_text, sizeof(ip_text));

    if (app_netx_http_append(body,
                             body_size,
                             &used,
                             "{\"board\":\"ART-Pi STM32H750\","
                             "\"api_version\":%u,\"firmware_version\":\"%s\","
                             "\"ip\":\"%s\",\"uptime_s\":%lu,"
                             "\"tcp_echo_port\":%u,\"http_port\":%u,"
                             "\"ethernet\":{\"rx_frames\":%lu,"
                             "\"tx_frames\":%lu,\"rx_drops\":%lu,"
                             "\"rx_errors\":%lu,\"tx_errors\":%lu,"
                             "\"dma_errors\":%lu,"
                             "\"tx_timeouts\":%lu,"
                             "\"tx_recoveries\":%lu,"
                             "\"last_hal_error\":%lu,"
                             "\"last_dma_status\":%lu},"
                             "\"rs485\":{\"role\":%u,"
                             "\"active_transaction\":%u,"
                             "\"active_unit_id\":%u,"
                             "\"active_function\":%u,"
                             "\"command_queue_depth\":%u,"
                             "\"rx_bytes\":%lu,\"rx_frames\":%lu,"
                             "\"replies\":%lu,\"crc_errors\":%lu,"
                             "\"malformed_frames\":%lu,"
                             "\"rtu_overflow_frames\":%lu,"
                             "\"rtu_dropped_frames\":%lu,"
                             "\"rtu_t15_violations\":%lu,"
                             "\"rtu_discarded_bytes\":%lu,"
                             "\"rtu_timestamp_errors\":%lu,"
                             "\"rtu_rx_errors\":%lu,"
                             "\"unsupported_rx_blocks\":%lu,"
                             "\"polls_completed\":%lu,"
                             "\"poll_timeouts\":%lu,"
                             "\"commands_queued\":%lu,"
                             "\"commands_completed\":%lu,"
                             "\"commands_failed\":%lu,"
                             "\"priority_dispatches\":%lu,"
                             "\"last_command\":{\"id\":%lu,"
                             "\"result\":\"%s\",\"unit_id\":%u,"
                             "\"function\":%u,\"exception\":%u},"
                             "\"devices\":[",
                             (unsigned int)APP_NETX_API_VERSION,
                             APP_NETX_FIRMWARE_VERSION,
                             ip_text,
                             (unsigned long)uptime_seconds,
                             (unsigned int)APP_NETX_TCP_ECHO_PORT,
                             (unsigned int)APP_NETX_HTTP_PORT,
                             (unsigned long)diagnostics.received_frames,
                             (unsigned long)diagnostics.transmitted_frames,
                             (unsigned long)diagnostics.receive_drops,
                             (unsigned long)diagnostics.receive_errors,
                             (unsigned long)diagnostics.transmit_errors,
                             (unsigned long)diagnostics.dma_errors,
                             (unsigned long)diagnostics.transmit_timeouts,
                             (unsigned long)diagnostics.transmit_recoveries,
                             (unsigned long)diagnostics.last_hal_error,
                             (unsigned long)diagnostics.last_dma_status,
                             (unsigned int)netx_modbus_diagnostics.role,
                             (unsigned int)netx_modbus_diagnostics.active_transaction,
                             (unsigned int)netx_modbus_diagnostics.active_unit_id,
                             (unsigned int)netx_modbus_diagnostics.active_function,
                             (unsigned int)netx_modbus_diagnostics.command_queue_depth,
                             (unsigned long)netx_modbus_diagnostics.received_bytes,
                             (unsigned long)netx_modbus_diagnostics.received_frames,
                             (unsigned long)netx_modbus_diagnostics.replied_frames,
                             (unsigned long)netx_modbus_diagnostics.crc_errors,
                             (unsigned long)netx_modbus_diagnostics.malformed_frames,
                             (unsigned long)netx_modbus_diagnostics.rtu_overflow_frames,
                             (unsigned long)netx_modbus_diagnostics.rtu_dropped_frames,
                             (unsigned long)netx_modbus_diagnostics.rtu_t15_violations,
                             (unsigned long)netx_modbus_diagnostics.rtu_discarded_bytes,
                             (unsigned long)netx_modbus_diagnostics.rtu_timestamp_errors,
                             (unsigned long)netx_modbus_diagnostics.rtu_rx_errors,
                             (unsigned long)netx_modbus_diagnostics.unsupported_rx_blocks,
                             (unsigned long)netx_modbus_diagnostics.polls_completed,
                             (unsigned long)netx_modbus_diagnostics.poll_timeouts,
                             (unsigned long)netx_modbus_diagnostics.commands_queued,
                             (unsigned long)netx_modbus_diagnostics.commands_completed,
                             (unsigned long)netx_modbus_diagnostics.commands_failed,
                             (unsigned long)netx_modbus_diagnostics.priority_dispatches,
                             (unsigned long)netx_modbus_diagnostics.last_command_id,
                             app_modbus_command_result_name(
                                 netx_modbus_diagnostics.last_command_result),
                             (unsigned int)netx_modbus_diagnostics.last_command_unit_id,
                             (unsigned int)netx_modbus_diagnostics.last_command_function,
                             (unsigned int)netx_modbus_diagnostics.last_command_exception) == 0U)
    {
        goto response_overflow;
    }

    for (device_index = 0U;
         device_index < netx_modbus_diagnostics.device_count;
         device_index++)
    {
        const app_modbus_master_device_status_t *device =
            &netx_modbus_diagnostics.devices[device_index];

        if (app_netx_http_append(body,
                                 body_size,
                                 &used,
                                 "%s{\"index\":%lu,\"unit_id\":%u,"
                                 "\"state\":\"%s\","
                                 "\"consecutive_failures\":%u,"
                                 "\"backoff_step\":%u,"
                                 "\"last_function\":%u,"
                                 "\"last_exception\":%u,"
                                 "\"successful_polls\":%lu,"
                                 "\"timeouts\":%lu,"
                                 "\"protocol_errors\":%lu,"
                                 "\"last_success_ms\":%lu,"
                                 "\"next_action_ms\":%lu,\"values\":[",
                                 (device_index == 0U) ? "" : ",",
                                 (unsigned long)device_index,
                                 (unsigned int)device->unit_id,
                                 app_modbus_device_state_name(device->state),
                                 (unsigned int)device->consecutive_failures,
                                 (unsigned int)device->backoff_step,
                                 (unsigned int)device->last_function,
                                 (unsigned int)device->last_exception,
                                 (unsigned long)device->successful_polls,
                                 (unsigned long)device->timeouts,
                                 (unsigned long)device->protocol_errors,
                                 (unsigned long)device->last_success_ms,
                                 (unsigned long)device->next_action_ms) == 0U)
        {
            goto response_overflow;
        }

        for (range_index = 0U;
             range_index < APP_DEVICE_CONFIG_REGISTER_CLASS_COUNT;
             range_index++)
        {
            uint16_t quantity = config.persistent
                .master_devices[device_index].ranges[range_index].quantity;

            if (app_netx_http_append(body,
                                     body_size,
                                     &used,
                                     "%s[",
                                     (range_index == 0U) ? "" : ",") == 0U)
            {
                goto response_overflow;
            }
            for (value_index = 0U; value_index < quantity; value_index++)
            {
                if (app_netx_http_append(body,
                                         body_size,
                                         &used,
                                         "%s%u",
                                         (value_index == 0U) ? "" : ",",
                                         (unsigned int)device->values[range_index][value_index]) == 0U)
                {
                    goto response_overflow;
                }
            }
            if (app_netx_http_append(body, body_size, &used, "]") == 0U)
            {
                goto response_overflow;
            }
        }
        if (app_netx_http_append(body, body_size, &used, "]}") == 0U)
        {
            goto response_overflow;
        }
    }

    if (app_netx_http_append(body, body_size, &used, "]}}\r\n") == 0U)
    {
        goto response_overflow;
    }
    return;

response_overflow:
    (void)snprintf(body,
                   body_size,
                   "{\"ok\":false,\"error\":\"status response too large\"}\r\n");
}

static uint8_t app_netx_http_append(char *body,
                                    size_t body_size,
                                    size_t *used,
                                    const char *format,
                                    ...)
{
    va_list arguments;
    int written;

    if ((body == NULL) || (used == NULL) || (format == NULL) ||
        (*used >= body_size))
    {
        return 0U;
    }

    va_start(arguments, format);
    written = vsnprintf(&body[*used], body_size - *used, format, arguments);
    va_end(arguments);
    if ((written < 0) || ((size_t)written >= (body_size - *used)))
    {
        body[body_size - 1U] = '\0';
        return 0U;
    }
    *used += (size_t)written;
    return 1U;
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
