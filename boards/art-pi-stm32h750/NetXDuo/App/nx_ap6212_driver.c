#include "nx_ap6212_driver.h"

#include <string.h>

#include "app_ap6212_sdio_probe.h"
#include "app_config.h"

#define NX_AP6212_ETHERNET_IP              0x0800U
#define NX_AP6212_ETHERNET_ARP             0x0806U
#define NX_AP6212_ETHERNET_RARP            0x8035U
#define NX_AP6212_ETHERNET_IPV6            0x86DDU
#define NX_AP6212_ETHERNET_HEADER_SIZE     14U
#define NX_AP6212_INTERFACE_MTU            1500U
#define NX_AP6212_RX_WAIT_TICKS            2U

static NX_IP *g_nx_ap6212_ip;
static NX_INTERFACE *g_nx_ap6212_interface;
static NX_PACKET_POOL *g_nx_ap6212_packet_pool;
static TX_THREAD g_nx_ap6212_rx_thread;
static UCHAR g_nx_ap6212_rx_stack[APP_AP6212_NETX_RX_STACK_SIZE];
static UCHAR g_nx_ap6212_tx_frame[APP_AP6212_NETX_PACKET_PAYLOAD];
static UCHAR g_nx_ap6212_rx_frame[APP_AP6212_NETX_PACKET_PAYLOAD];
static uint8_t g_nx_ap6212_mac[6];
static uint8_t g_nx_ap6212_initialized;
static uint8_t g_nx_ap6212_enabled;
static uint8_t g_nx_ap6212_rx_thread_created;

static uint16_t nx_ap6212_u16_be(const UCHAR *data)
{
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static void nx_ap6212_put_u16_be(UCHAR *data, uint16_t value)
{
    data[0] = (UCHAR)(value >> 8);
    data[1] = (UCHAR)value;
}

static void nx_ap6212_mac_to_netx(const uint8_t mac[6],
                                  ULONG *physical_msw,
                                  ULONG *physical_lsw)
{
    *physical_msw = ((ULONG)mac[0] << 8) | (ULONG)mac[1];
    *physical_lsw = ((ULONG)mac[2] << 24) |
                    ((ULONG)mac[3] << 16) |
                    ((ULONG)mac[4] << 8) |
                    (ULONG)mac[5];
}

static void nx_ap6212_netx_to_mac(ULONG physical_msw,
                                  ULONG physical_lsw,
                                  UCHAR mac[6])
{
    mac[0] = (UCHAR)(physical_msw >> 8);
    mac[1] = (UCHAR)physical_msw;
    mac[2] = (UCHAR)(physical_lsw >> 24);
    mac[3] = (UCHAR)(physical_lsw >> 16);
    mac[4] = (UCHAR)(physical_lsw >> 8);
    mac[5] = (UCHAR)physical_lsw;
}

static uint16_t nx_ap6212_ether_type(const NX_IP_DRIVER *driver_req_ptr,
                                     const NX_PACKET *packet_ptr)
{
    if(driver_req_ptr->nx_ip_driver_command == NX_LINK_ARP_SEND ||
       driver_req_ptr->nx_ip_driver_command == NX_LINK_ARP_RESPONSE_SEND)
    {
        return NX_AP6212_ETHERNET_ARP;
    }
    if(driver_req_ptr->nx_ip_driver_command == NX_LINK_RARP_SEND)
        return NX_AP6212_ETHERNET_RARP;
    if(packet_ptr != NX_NULL && packet_ptr->nx_packet_ip_version == NX_IP_VERSION_V4)
        return NX_AP6212_ETHERNET_IP;

    return NX_AP6212_ETHERNET_IPV6;
}

static UINT nx_ap6212_packet_send(NX_IP_DRIVER *driver_req_ptr)
{
    NX_PACKET *packet_ptr = driver_req_ptr->nx_ip_driver_packet;
    ULONG copied = 0U;
    uint16_t ether_type;
    uint32_t frame_length;
    UINT status;

    if(packet_ptr == NX_NULL || g_nx_ap6212_enabled == 0U)
        return NX_NOT_ENABLED;

    ether_type = nx_ap6212_ether_type(driver_req_ptr, packet_ptr);
    if(ether_type == NX_AP6212_ETHERNET_IPV6)
        return NX_NOT_SUCCESSFUL;

    if(driver_req_ptr->nx_ip_driver_command == NX_LINK_PACKET_BROADCAST)
    {
        memset(&g_nx_ap6212_tx_frame[0], 0xFF, 6U);
    }
    else
    {
        nx_ap6212_netx_to_mac(driver_req_ptr->nx_ip_driver_physical_address_msw,
                              driver_req_ptr->nx_ip_driver_physical_address_lsw,
                              &g_nx_ap6212_tx_frame[0]);
    }

    memcpy(&g_nx_ap6212_tx_frame[6], g_nx_ap6212_mac, 6U);
    nx_ap6212_put_u16_be(&g_nx_ap6212_tx_frame[12], ether_type);

    status = nx_packet_data_extract_offset(packet_ptr,
                                           0U,
                                           &g_nx_ap6212_tx_frame[NX_AP6212_ETHERNET_HEADER_SIZE],
                                           sizeof(g_nx_ap6212_tx_frame) -
                                           NX_AP6212_ETHERNET_HEADER_SIZE,
                                           &copied);
    if(status == NX_SUCCESS)
    {
        frame_length = NX_AP6212_ETHERNET_HEADER_SIZE + copied;
        if(frame_length > 0xFFFFU ||
           app_ap6212_wifi_send_ethernet(g_nx_ap6212_tx_frame,
                                         (uint16_t)frame_length) != 0)
        {
            status = NX_NOT_SUCCESSFUL;
        }
    }

    nx_packet_transmit_release(packet_ptr);
    return status;
}

static void nx_ap6212_receive_to_netx(const UCHAR *frame, uint16_t length)
{
    NX_PACKET *packet_ptr;
    uint16_t ether_type;

    if(g_nx_ap6212_ip == NX_NULL ||
       g_nx_ap6212_interface == NX_NULL ||
       g_nx_ap6212_packet_pool == NX_NULL ||
       length <= NX_AP6212_ETHERNET_HEADER_SIZE)
    {
        return;
    }

    if(nx_packet_allocate(g_nx_ap6212_packet_pool,
                          &packet_ptr,
                          NX_RECEIVE_PACKET,
                          NX_NO_WAIT) != NX_SUCCESS)
    {
        return;
    }

    /* Align IPv4 header after the 14-byte Ethernet header. */
    packet_ptr->nx_packet_prepend_ptr += 2U;
    packet_ptr->nx_packet_append_ptr += 2U;

    if(nx_packet_data_append(packet_ptr,
                             (VOID *)frame,
                             length,
                             g_nx_ap6212_packet_pool,
                             NX_NO_WAIT) != NX_SUCCESS)
    {
        nx_packet_release(packet_ptr);
        return;
    }

    ether_type = nx_ap6212_u16_be(&packet_ptr->nx_packet_prepend_ptr[12]);
    packet_ptr->nx_packet_ip_interface = g_nx_ap6212_interface;
    packet_ptr->nx_packet_prepend_ptr += NX_AP6212_ETHERNET_HEADER_SIZE;
    packet_ptr->nx_packet_length -= NX_AP6212_ETHERNET_HEADER_SIZE;

    if(ether_type == NX_AP6212_ETHERNET_IP)
    {
        _nx_ip_packet_receive(g_nx_ap6212_ip, packet_ptr);
    }
    else if(ether_type == NX_AP6212_ETHERNET_ARP)
    {
        if(g_nx_ap6212_interface->nx_interface_ip_address == 0U)
        {
            nx_packet_release(packet_ptr);
            return;
        }
        _nx_arp_packet_deferred_receive(g_nx_ap6212_ip, packet_ptr);
    }
    else if(ether_type == NX_AP6212_ETHERNET_RARP)
    {
        _nx_rarp_packet_deferred_receive(g_nx_ap6212_ip, packet_ptr);
    }
    else
    {
        nx_packet_release(packet_ptr);
    }
}

static VOID nx_ap6212_rx_entry(ULONG thread_input)
{
    uint16_t length;

    (void)thread_input;

    for(;;)
    {
        if(g_nx_ap6212_enabled == 0U)
        {
            tx_thread_sleep(10U);
            continue;
        }

        length = 0U;
        if(app_ap6212_wifi_receive_ethernet(g_nx_ap6212_rx_frame,
                                            sizeof(g_nx_ap6212_rx_frame),
                                            &length,
                                            NX_AP6212_RX_WAIT_TICKS) == 0)
        {
            nx_ap6212_receive_to_netx(g_nx_ap6212_rx_frame, length);
        }
        else
        {
            tx_thread_sleep(1U);
        }
    }
}

static UINT nx_ap6212_start_rx_thread(void)
{
    UINT status;

    if(g_nx_ap6212_rx_thread_created != 0U)
        return NX_SUCCESS;

    status = tx_thread_create(&g_nx_ap6212_rx_thread,
                              "AP6212 NetX RX",
                              nx_ap6212_rx_entry,
                              0U,
                              g_nx_ap6212_rx_stack,
                              sizeof(g_nx_ap6212_rx_stack),
                              APP_AP6212_NETX_RX_PRIO,
                              APP_AP6212_NETX_RX_PRIO,
                              TX_NO_TIME_SLICE,
                              TX_AUTO_START);
    if(status != TX_SUCCESS)
        return NX_NOT_SUCCESSFUL;

    g_nx_ap6212_rx_thread_created = 1U;
    return NX_SUCCESS;
}

VOID nx_ap6212_driver(NX_IP_DRIVER *driver_req_ptr)
{
    ULONG physical_msw;
    ULONG physical_lsw;
    UINT interface_index = 0U;

    driver_req_ptr->nx_ip_driver_status = NX_SUCCESS;

    if(driver_req_ptr->nx_ip_driver_interface != NX_NULL)
        interface_index = driver_req_ptr->nx_ip_driver_interface->nx_interface_index;

    switch(driver_req_ptr->nx_ip_driver_command)
    {
    case NX_LINK_INTERFACE_ATTACH:
        g_nx_ap6212_ip = driver_req_ptr->nx_ip_driver_ptr;
        g_nx_ap6212_interface = driver_req_ptr->nx_ip_driver_interface;
        break;

    case NX_LINK_INITIALIZE:
        if(app_ap6212_wifi_get_mac(g_nx_ap6212_mac) != 0)
        {
            driver_req_ptr->nx_ip_driver_status = NX_NOT_SUCCESSFUL;
            break;
        }

        g_nx_ap6212_ip = driver_req_ptr->nx_ip_driver_ptr;
        g_nx_ap6212_interface = driver_req_ptr->nx_ip_driver_interface;
        g_nx_ap6212_packet_pool = driver_req_ptr->nx_ip_driver_ptr->nx_ip_default_packet_pool;
        nx_ap6212_mac_to_netx(g_nx_ap6212_mac, &physical_msw, &physical_lsw);
        (void)nx_ip_interface_mtu_set(driver_req_ptr->nx_ip_driver_ptr,
                                      interface_index,
                                      NX_AP6212_INTERFACE_MTU);
        (void)nx_ip_interface_physical_address_set(driver_req_ptr->nx_ip_driver_ptr,
                                                   interface_index,
                                                   physical_msw,
                                                   physical_lsw,
                                                   NX_FALSE);
        (void)nx_ip_interface_address_mapping_configure(driver_req_ptr->nx_ip_driver_ptr,
                                                        interface_index,
                                                        NX_TRUE);
        g_nx_ap6212_initialized = 1U;
        break;

    case NX_LINK_ENABLE:
        if(g_nx_ap6212_initialized == 0U)
        {
            driver_req_ptr->nx_ip_driver_status = NX_NOT_ENABLED;
            break;
        }
        g_nx_ap6212_enabled = 1U;
        if(g_nx_ap6212_interface != NX_NULL)
            g_nx_ap6212_interface->nx_interface_link_up = NX_TRUE;
        driver_req_ptr->nx_ip_driver_status = nx_ap6212_start_rx_thread();
        break;

    case NX_LINK_DISABLE:
        g_nx_ap6212_enabled = 0U;
        if(g_nx_ap6212_interface != NX_NULL)
            g_nx_ap6212_interface->nx_interface_link_up = NX_FALSE;
        break;

    case NX_LINK_UNINITIALIZE:
        g_nx_ap6212_enabled = 0U;
        g_nx_ap6212_initialized = 0U;
        if(g_nx_ap6212_interface != NX_NULL)
            g_nx_ap6212_interface->nx_interface_link_up = NX_FALSE;
        break;

    case NX_LINK_PACKET_SEND:
    case NX_LINK_PACKET_BROADCAST:
    case NX_LINK_ARP_SEND:
    case NX_LINK_ARP_RESPONSE_SEND:
    case NX_LINK_RARP_SEND:
        driver_req_ptr->nx_ip_driver_status = nx_ap6212_packet_send(driver_req_ptr);
        break;

    case NX_LINK_MULTICAST_JOIN:
    case NX_LINK_MULTICAST_LEAVE:
        break;

    case NX_LINK_GET_STATUS:
        if(driver_req_ptr->nx_ip_driver_return_ptr != NX_NULL)
        {
            *(driver_req_ptr->nx_ip_driver_return_ptr) =
                (g_nx_ap6212_enabled != 0U) ? NX_TRUE : NX_FALSE;
        }
        break;

    case NX_LINK_GET_INTERFACE_TYPE:
        if(driver_req_ptr->nx_ip_driver_return_ptr != NX_NULL)
            *(driver_req_ptr->nx_ip_driver_return_ptr) = NX_INTERFACE_TYPE_WIFI;
        break;

    case NX_LINK_GET_SPEED:
    case NX_LINK_GET_DUPLEX_TYPE:
    case NX_LINK_GET_ERROR_COUNT:
    case NX_LINK_GET_RX_COUNT:
    case NX_LINK_GET_TX_COUNT:
    case NX_LINK_GET_ALLOC_ERRORS:
    case NX_INTERFACE_CAPABILITY_GET:
    case NX_INTERFACE_CAPABILITY_SET:
        if(driver_req_ptr->nx_ip_driver_return_ptr != NX_NULL)
            *(driver_req_ptr->nx_ip_driver_return_ptr) = 0U;
        break;

    default:
        driver_req_ptr->nx_ip_driver_status = NX_NOT_SUCCESSFUL;
        break;
    }
}
