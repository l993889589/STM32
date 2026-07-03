#include "app_netxduo.h"

#include <stdio.h>

#include "app_ap6212_sdio_probe.h"
#include "app_config.h"
#include "app_uart4_console.h"
#include "nx_api.h"
#include "nx_ap6212_driver.h"
#include "nxd_dhcp_client.h"

#ifndef TX_TIMER_TICKS_PER_SECOND
#define TX_TIMER_TICKS_PER_SECOND       1000U
#endif

#define APP_NETX_WAIT_AP_TICKS          (TX_TIMER_TICKS_PER_SECOND / 10U)
#define APP_NETX_DHCP_WAIT_TICKS        (60U * TX_TIMER_TICKS_PER_SECOND)
#define APP_NETX_PING_WAIT_TICKS        (5U * TX_TIMER_TICKS_PER_SECOND)

static TX_THREAD g_app_netxduo_thread;
static UCHAR g_app_netxduo_thread_stack[APP_AP6212_NETX_THREAD_STACK_SIZE];
static NX_PACKET_POOL g_app_netxduo_packet_pool;
static NX_IP g_app_netxduo_ip;
static NX_DHCP g_app_netxduo_dhcp;
static UCHAR g_app_netxduo_ip_stack[APP_AP6212_NETX_IP_STACK_SIZE];
static UCHAR g_app_netxduo_arp_cache[APP_AP6212_NETX_ARP_CACHE_SIZE];
static ULONG g_app_netxduo_packet_pool_area[(APP_AP6212_NETX_PACKET_POOL_SIZE +
                                             sizeof(ULONG) - 1U) /
                                            sizeof(ULONG)];
static uint8_t g_app_netxduo_started;

static void app_netxduo_print_ip(const char *prefix, ULONG ip)
{
    (void)app_uart4_console_printf("%s%lu.%lu.%lu.%lu\r\n",
                                   prefix,
                                   (unsigned long)((ip >> 24) & 0xFFU),
                                   (unsigned long)((ip >> 16) & 0xFFU),
                                   (unsigned long)((ip >> 8) & 0xFFU),
                                   (unsigned long)(ip & 0xFFU));
}

static ULONG app_netxduo_read_dns(void)
{
    ULONG dns = 0U;
    UINT dns_size = sizeof(dns);

    if(nx_dhcp_user_option_retrieve(&g_app_netxduo_dhcp,
                                    NX_DHCP_OPTION_DNS_SVR,
                                    (UCHAR *)&dns,
                                    &dns_size) != NX_SUCCESS ||
       dns_size < sizeof(dns))
    {
        return 0U;
    }

    return dns;
}

static UINT app_netxduo_create_stack(void)
{
    UINT status;

    nx_system_initialize();

    status = nx_packet_pool_create(&g_app_netxduo_packet_pool,
                                   "AP6212 Packet Pool",
                                   APP_AP6212_NETX_PACKET_PAYLOAD,
                                   g_app_netxduo_packet_pool_area,
                                   sizeof(g_app_netxduo_packet_pool_area));
    if(status != NX_SUCCESS)
        return status;

    status = nx_ip_create(&g_app_netxduo_ip,
                          "AP6212 NetX IP",
                          0U,
                          0U,
                          &g_app_netxduo_packet_pool,
                          nx_ap6212_driver,
                          g_app_netxduo_ip_stack,
                          sizeof(g_app_netxduo_ip_stack),
                          APP_AP6212_NETX_IP_PRIO);
    if(status != NX_SUCCESS)
        return status;

    status = nx_arp_enable(&g_app_netxduo_ip,
                           g_app_netxduo_arp_cache,
                           sizeof(g_app_netxduo_arp_cache));
    if(status != NX_SUCCESS)
        return status;

    status = nx_icmp_enable(&g_app_netxduo_ip);
    if(status != NX_SUCCESS)
        return status;

    status = nx_udp_enable(&g_app_netxduo_ip);
    if(status != NX_SUCCESS)
        return status;

    status = nx_dhcp_create(&g_app_netxduo_dhcp,
                            &g_app_netxduo_ip,
                            "AP6212 DHCP");
    if(status != NX_SUCCESS)
        return status;

    (void)nx_dhcp_user_option_request(&g_app_netxduo_dhcp,
                                      NX_DHCP_OPTION_DNS_SVR);
    return NX_SUCCESS;
}

static void app_netxduo_thread_entry(ULONG thread_input)
{
    NX_PACKET *response = NX_NULL;
    ULONG actual_status = 0U;
    ULONG ip_address = 0U;
    ULONG netmask = 0U;
    ULONG gateway = 0U;
    ULONG dns = 0U;
    UINT status;

    (void)thread_input;

    (void)app_uart4_console_write_string("[netx] wait AP6212 link\r\n");
    while(app_ap6212_wifi_is_ready() == 0U)
        tx_thread_sleep(APP_NETX_WAIT_AP_TICKS);

    (void)app_uart4_console_write_string("[netx] create stack\r\n");
    status = app_netxduo_create_stack();
    if(status != NX_SUCCESS)
    {
        (void)app_uart4_console_printf("[netx] create failed status=0x%02X\r\n",
                                       (unsigned int)status);
        return;
    }

    status = nx_ip_driver_direct_command(&g_app_netxduo_ip,
                                         NX_LINK_ENABLE,
                                         &actual_status);
    if(status != NX_SUCCESS)
    {
        (void)app_uart4_console_printf("[netx] link enable failed status=0x%02X\r\n",
                                       (unsigned int)status);
        return;
    }

    (void)app_uart4_console_write_string("[netx] dhcp start\r\n");
    status = nx_dhcp_start(&g_app_netxduo_dhcp);
    if(status != NX_SUCCESS)
    {
        (void)app_uart4_console_printf("[netx] dhcp start failed status=0x%02X\r\n",
                                       (unsigned int)status);
        return;
    }

    status = nx_ip_status_check(&g_app_netxduo_ip,
                                NX_IP_ADDRESS_RESOLVED,
                                &actual_status,
                                APP_NETX_DHCP_WAIT_TICKS);
    if(status != NX_SUCCESS)
    {
        (void)app_uart4_console_printf("[netx] dhcp timeout status=0x%02X actual=0x%08lX\r\n",
                                       (unsigned int)status,
                                       (unsigned long)actual_status);
        return;
    }

    (void)nx_ip_address_get(&g_app_netxduo_ip, &ip_address, &netmask);
    (void)nx_ip_gateway_address_get(&g_app_netxduo_ip, &gateway);
    dns = app_netxduo_read_dns();

    app_netxduo_print_ip("[netx] ip=", ip_address);
    app_netxduo_print_ip("[netx] mask=", netmask);
    app_netxduo_print_ip("[netx] gateway=", gateway);
    app_netxduo_print_ip("[netx] dns=", dns);

    status = nx_icmp_ping(&g_app_netxduo_ip,
                          gateway,
                          "ap6212-netx",
                          11U,
                          &response,
                          APP_NETX_PING_WAIT_TICKS);
    if(status == NX_SUCCESS)
    {
        (void)app_uart4_console_write_string("[netx] ping gateway OK\r\n");
        if(response != NX_NULL)
            (void)nx_packet_release(response);
    }
    else
    {
        (void)app_uart4_console_printf("[netx] ping gateway failed status=0x%02X\r\n",
                                       (unsigned int)status);
    }

    for(;;)
        tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
}

UINT app_netxduo_init(void)
{
    UINT status;

    if(g_app_netxduo_started != 0U)
        return TX_SUCCESS;

    status = tx_thread_create(&g_app_netxduo_thread,
                              "AP6212 NetX",
                              app_netxduo_thread_entry,
                              0U,
                              g_app_netxduo_thread_stack,
                              sizeof(g_app_netxduo_thread_stack),
                              APP_AP6212_NETX_THREAD_PRIO,
                              APP_AP6212_NETX_THREAD_PRIO,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status == TX_SUCCESS)
        g_app_netxduo_started = 1U;

    return status;
}
