#include "includes.h"
#include "app_netx.h"

#include "nx_ip.h"

#define APP_NETX_PACKET_PAYLOAD_SIZE       1536U
#define APP_NETX_PACKET_POOL_SIZE         32768U
#define APP_NETX_IP_STACK_SIZE             4096U
#define APP_NETX_ARP_CACHE_SIZE            1024U
#define APP_NETX_LINK_STACK_SIZE           2048U
#define APP_NETX_LINK_THREAD_PRIORITY         4U
#define APP_NETX_LINK_POLL_TICKS             50U

#define APP_NETX_ETHERNET_HEADER_SIZE        14U
#define APP_NETX_ETHERTYPE_IPV4          0x0800U
#define APP_NETX_ETHERTYPE_ARP           0x0806U
#define APP_NETX_ETHERTYPE_RARP          0x8035U

static NX_PACKET_POOL netx_packet_pool;
static NX_IP netx_ip;
static TX_THREAD netx_link_thread;
static NX_IP *netx_driver_ip;
static NX_INTERFACE *netx_driver_interface;
static uint8_t netx_started;

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_packet_pool_memory[APP_NETX_PACKET_POOL_SIZE];

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_ip_stack[APP_NETX_IP_STACK_SIZE];

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_arp_cache[APP_NETX_ARP_CACHE_SIZE];

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_link_stack[APP_NETX_LINK_STACK_SIZE];

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_tx_frame[BSP_ETH_FRAME_MAX_SIZE];

__attribute__((section(".bss.netx"), aligned(32)))
static uint8_t netx_rx_frame[BSP_ETH_FRAME_MAX_SIZE];

static void app_netx_driver(NX_IP_DRIVER *request);
static void app_netx_send(NX_IP_DRIVER *request);
static void app_netx_receive(void);
static void app_netx_rx_ready(void *argument);
static void app_netx_link_thread_entry(ULONG input);
static void app_netx_report_link(const bsp_eth_link_t *link);

UINT app_netx_start(void)
{
    UINT status;
    char message[160];

    if (netx_started != 0U)
    {
        return NX_SUCCESS;
    }

    nx_system_initialize();

    status = nx_packet_pool_create(&netx_packet_pool,
                                   "ethernet_packet_pool",
                                   APP_NETX_PACKET_PAYLOAD_SIZE,
                                   netx_packet_pool_memory,
                                   sizeof(netx_packet_pool_memory));
    if (status != NX_SUCCESS)
    {
        return status;
    }

    status = nx_ip_create(&netx_ip,
                          "art_pi_ethernet",
                          APP_NETX_IP_ADDRESS,
                          APP_NETX_NETWORK_MASK,
                          &netx_packet_pool,
                          app_netx_driver,
                          netx_ip_stack,
                          sizeof(netx_ip_stack),
                          3U);
    if (status != NX_SUCCESS)
    {
        return status;
    }

    status = nx_arp_enable(&netx_ip,
                           netx_arp_cache,
                           sizeof(netx_arp_cache));
    if (status != NX_SUCCESS)
    {
        return status;
    }

    status = nx_icmp_enable(&netx_ip);
    if (status != NX_SUCCESS)
    {
        return status;
    }

    status = tx_thread_create(&netx_link_thread,
                              "ethernet_link_monitor",
                              app_netx_link_thread_entry,
                              0UL,
                              netx_link_stack,
                              sizeof(netx_link_stack),
                              APP_NETX_LINK_THREAD_PRIORITY,
                              APP_NETX_LINK_THREAD_PRIORITY,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if (status != TX_SUCCESS)
    {
        return status;
    }

    netx_started = 1U;
    (void)snprintf(message,
                   sizeof(message),
                   "NetX Duo ready: IPv4 192.168.1.50/24, MAC "
                   "02:80:E1:75:00:01\r\n");
    bsp_uart_write_string(BSP_UART_DEBUG, message);
    return NX_SUCCESS;
}

static void app_netx_driver(NX_IP_DRIVER *request)
{
    NX_INTERFACE *interface = request->nx_ip_driver_interface;
    uint8_t mac[6];

    request->nx_ip_driver_status = NX_SUCCESS;

    switch (request->nx_ip_driver_command)
    {
        case NX_LINK_INTERFACE_ATTACH:
            netx_driver_ip = request->nx_ip_driver_ptr;
            netx_driver_interface = interface;
            break;

        case NX_LINK_INITIALIZE:
            if (bsp_eth_init() != HAL_OK)
            {
                request->nx_ip_driver_status = NX_NOT_SUCCESSFUL;
                break;
            }
            bsp_eth_get_mac_address(mac);
            interface->nx_interface_ip_mtu_size = 1500U;
            interface->nx_interface_physical_address_msw =
                ((ULONG)mac[0] << 8) | (ULONG)mac[1];
            interface->nx_interface_physical_address_lsw =
                ((ULONG)mac[2] << 24) | ((ULONG)mac[3] << 16) |
                ((ULONG)mac[4] << 8) | (ULONG)mac[5];
            interface->nx_interface_address_mapping_needed = NX_TRUE;
            bsp_eth_set_rx_callback(app_netx_rx_ready, NULL);
            break;

        case NX_LINK_ENABLE:
        {
            bsp_eth_link_t link;

            if (bsp_eth_start() != HAL_OK)
            {
                request->nx_ip_driver_status = NX_NOT_SUCCESSFUL;
                break;
            }
            if ((bsp_eth_get_link(&link) == HAL_OK) &&
                (link.link_up != 0U))
            {
                (void)bsp_eth_apply_link(&link);
                interface->nx_interface_link_up = NX_TRUE;
            }
            else
            {
                interface->nx_interface_link_up = NX_FALSE;
            }
            break;
        }

        case NX_LINK_DISABLE:
            (void)bsp_eth_stop();
            interface->nx_interface_link_up = NX_FALSE;
            break;

        case NX_LINK_PACKET_SEND:
        case NX_LINK_PACKET_BROADCAST:
        case NX_LINK_ARP_SEND:
        case NX_LINK_ARP_RESPONSE_SEND:
        case NX_LINK_RARP_SEND:
            app_netx_send(request);
            break;

        case NX_LINK_DEFERRED_PROCESSING:
            app_netx_receive();
            break;

        case NX_LINK_GET_STATUS:
            *request->nx_ip_driver_return_ptr = interface->nx_interface_link_up;
            break;

        case NX_LINK_MULTICAST_JOIN:
        case NX_LINK_MULTICAST_LEAVE:
            break;

        default:
            request->nx_ip_driver_status = NX_UNHANDLED_COMMAND;
            break;
    }
}

static void app_netx_send(NX_IP_DRIVER *request)
{
    NX_PACKET *packet = request->nx_ip_driver_packet;
    NX_PACKET *part;
    ULONG *header;
    uint32_t copied = 0U;
    uint16_t ether_type;

    packet->nx_packet_prepend_ptr -= APP_NETX_ETHERNET_HEADER_SIZE;
    packet->nx_packet_length += APP_NETX_ETHERNET_HEADER_SIZE;
    header = (ULONG *)(packet->nx_packet_prepend_ptr - 2U);

    header[0] = request->nx_ip_driver_physical_address_msw;
    header[1] = request->nx_ip_driver_physical_address_lsw;
    header[2] = (netx_driver_interface->nx_interface_physical_address_msw << 16) |
                (netx_driver_interface->nx_interface_physical_address_lsw >> 16);

    if ((request->nx_ip_driver_command == NX_LINK_ARP_SEND) ||
        (request->nx_ip_driver_command == NX_LINK_ARP_RESPONSE_SEND))
    {
        ether_type = APP_NETX_ETHERTYPE_ARP;
    }
    else if (request->nx_ip_driver_command == NX_LINK_RARP_SEND)
    {
        ether_type = APP_NETX_ETHERTYPE_RARP;
    }
    else
    {
        ether_type = APP_NETX_ETHERTYPE_IPV4;
    }

    header[3] = (netx_driver_interface->nx_interface_physical_address_lsw << 16) |
                (ULONG)ether_type;
    NX_CHANGE_ULONG_ENDIAN(header[0]);
    NX_CHANGE_ULONG_ENDIAN(header[1]);
    NX_CHANGE_ULONG_ENDIAN(header[2]);
    NX_CHANGE_ULONG_ENDIAN(header[3]);

    for (part = packet; part != NX_NULL; part = part->nx_packet_next)
    {
        uint32_t bytes = (uint32_t)(part->nx_packet_append_ptr -
                                    part->nx_packet_prepend_ptr);

        if (bytes > (sizeof(netx_tx_frame) - copied))
        {
            request->nx_ip_driver_status = NX_NOT_SUCCESSFUL;
            break;
        }
        memcpy(&netx_tx_frame[copied], part->nx_packet_prepend_ptr, bytes);
        copied += bytes;
    }

    if ((request->nx_ip_driver_status == NX_SUCCESS) &&
        (bsp_eth_transmit(netx_tx_frame, copied) != HAL_OK))
    {
        request->nx_ip_driver_status = NX_NOT_SUCCESSFUL;
    }

    packet->nx_packet_prepend_ptr += APP_NETX_ETHERNET_HEADER_SIZE;
    packet->nx_packet_length -= APP_NETX_ETHERNET_HEADER_SIZE;
    nx_packet_transmit_release(packet);
}

static void app_netx_receive(void)
{
    uint32_t frame_length;

    while (bsp_eth_receive(netx_rx_frame,
                           sizeof(netx_rx_frame),
                           &frame_length) == HAL_OK)
    {
        NX_PACKET *packet;
        uint16_t ether_type;

        if ((frame_length < APP_NETX_ETHERNET_HEADER_SIZE) ||
            (nx_packet_allocate(&netx_packet_pool,
                                &packet,
                                NX_RECEIVE_PACKET,
                                NX_NO_WAIT) != NX_SUCCESS))
        {
            continue;
        }

        /* Keep the IPv4 header 32-bit aligned after removing 14-byte Ethernet header. */
        packet->nx_packet_prepend_ptr += 2U;

        if ((ULONG)(packet->nx_packet_data_end - packet->nx_packet_prepend_ptr) <
            frame_length)
        {
            nx_packet_release(packet);
            continue;
        }

        memcpy(packet->nx_packet_prepend_ptr, netx_rx_frame, frame_length);
        packet->nx_packet_append_ptr = packet->nx_packet_prepend_ptr + frame_length;
        packet->nx_packet_length = frame_length;
        packet->nx_packet_ip_interface = netx_driver_interface;
        ether_type = ((uint16_t)netx_rx_frame[12] << 8) | netx_rx_frame[13];

        packet->nx_packet_prepend_ptr += APP_NETX_ETHERNET_HEADER_SIZE;
        packet->nx_packet_length -= APP_NETX_ETHERNET_HEADER_SIZE;

        if (ether_type == APP_NETX_ETHERTYPE_IPV4)
        {
            _nx_ip_packet_deferred_receive(netx_driver_ip, packet);
        }
        else if (ether_type == APP_NETX_ETHERTYPE_ARP)
        {
            _nx_arp_packet_deferred_receive(netx_driver_ip, packet);
        }
        else if (ether_type == APP_NETX_ETHERTYPE_RARP)
        {
            _nx_rarp_packet_deferred_receive(netx_driver_ip, packet);
        }
        else
        {
            nx_packet_release(packet);
        }
    }
}

static void app_netx_rx_ready(void *argument)
{
    (void)argument;

    if (netx_driver_ip != NX_NULL)
    {
        _nx_ip_driver_deferred_processing(netx_driver_ip);
    }
}

static void app_netx_link_thread_entry(ULONG input)
{
    uint8_t previous_link = 0xFFU;

    (void)input;

    while (1)
    {
        bsp_eth_link_t link;

        if ((bsp_eth_get_link(&link) == HAL_OK) &&
            (netx_driver_interface != NX_NULL) &&
            (link.link_up != previous_link))
        {
            if (link.link_up != 0U)
            {
                (void)bsp_eth_apply_link(&link);
                netx_driver_interface->nx_interface_link_up = NX_TRUE;
            }
            else
            {
                netx_driver_interface->nx_interface_link_up = NX_FALSE;
            }
            previous_link = link.link_up;
            _nx_ip_driver_link_status_event(&netx_ip, 0U);
            app_netx_report_link(&link);
        }

        tx_thread_sleep(APP_NETX_LINK_POLL_TICKS);
    }
}

static void app_netx_report_link(const bsp_eth_link_t *link)
{
    char message[96];

    if (link->link_up == 0U)
    {
        bsp_uart_write_string(BSP_UART_DEBUG,
                              "Ethernet link down\r\n");
        return;
    }

    (void)snprintf(message,
                   sizeof(message),
                   "Ethernet link up: PHY%lu, %u Mbps %s duplex\r\n",
                   (unsigned long)bsp_eth_get_phy_address(),
                   (unsigned int)link->speed_mbps,
                   (link->full_duplex != 0U) ? "full" : "half");
    bsp_uart_write_string(BSP_UART_DEBUG, message);
}
