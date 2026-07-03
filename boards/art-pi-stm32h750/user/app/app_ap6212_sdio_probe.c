#include "app_ap6212_sdio_probe.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "app_ap6212_fw_bundle.h"
#include "app_config.h"
#include "app_qspi_loader.h"
#include "app_uart4_console.h"
#include "bsp_ap6212_sdio.h"
#include "bsp_qspi_flash.h"

static uint8_t g_ap6212_fw_chunk[512];
static uint8_t g_ap6212_fw_verify[512];
static uint8_t g_ap6212_nvram_buffer[2048];
static uint8_t g_ap6212_nvram_verify[2048];
static uint8_t g_ap6212_shared_buffer[64];
#define APP_AP6212_SDPCM_BUFFER_SIZE        1728U
#define APP_AP6212_ETHERNET_BUFFER_SIZE     1536U
static uint8_t g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_BUFFER_SIZE];
static uint8_t g_ap6212_sdpcm_rx[APP_AP6212_SDPCM_BUFFER_SIZE];
static uint8_t g_ap6212_bcdc_payload[APP_AP6212_SDPCM_BUFFER_SIZE];
static uint8_t g_ap6212_bcdc_data[160];
static uint8_t g_ap6212_net_tx[APP_AP6212_ETHERNET_BUFFER_SIZE];
static uint8_t g_ap6212_net_rx[APP_AP6212_ETHERNET_BUFFER_SIZE];
static uint8_t g_ap6212_wifi_mac[6];
static uint8_t g_ap6212_gateway_mac[6];
static uint32_t g_ap6212_ip_addr = 0U;
static uint32_t g_ap6212_net_mask = 0U;
static uint32_t g_ap6212_gateway = 0U;
static uint32_t g_ap6212_dns = 0U;
static uint32_t g_ap6212_dhcp_server = 0U;
static uint8_t g_ap6212_sdpcm_tx_seq = 0U;
static uint8_t g_ap6212_sdpcm_readahead = 0U;
static uint16_t g_ap6212_bcdc_reqid = 0U;
static uint32_t g_ap6212_sdio_base = 0U;
static uint8_t g_ap6212_wifi_ready = 0U;
static TX_MUTEX g_ap6212_io_mutex;
static uint8_t g_ap6212_io_mutex_ready = 0U;
static TX_THREAD g_ap6212_sdio_probe_thread;
static UCHAR g_ap6212_sdio_probe_stack[APP_AP6212_SDIO_PROBE_STACK_SIZE];
volatile uint32_t app_ap6212_sdio_probe_done = 0U;
volatile int32_t app_ap6212_sdio_probe_status = 0x7FFFFFFF;
volatile bsp_ap6212_sdio_probe_t app_ap6212_sdio_probe_result;
volatile int32_t app_qspi_flash_probe_status = 0x7FFFFFFF;
volatile bsp_qspi_flash_id_t app_qspi_flash_probe_id;

static uint32_t probe_get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void probe_put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t probe_get_u16_be(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static uint32_t probe_get_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void probe_put_u16_be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void probe_put_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t app_ap6212_ip_checksum(const uint8_t *data, uint16_t length)
{
    uint32_t sum = 0U;

    while(length > 1U)
    {
        sum += ((uint16_t)data[0] << 8) | (uint16_t)data[1];
        data += 2;
        length -= 2U;
    }
    if(length != 0U)
        sum += ((uint16_t)data[0] << 8);

    while((sum >> 16) != 0U)
        sum = (sum & 0xFFFFU) + (sum >> 16);

    return (uint16_t)~sum;
}

static void probe_write(const char *line)
{
    (void)app_uart4_console_write_string(line);
}

static void probe_printf(const char *fmt, ...)
{
    char line[320];
    va_list args;
    int length;

    va_start(args, fmt);
    length = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if(length <= 0)
        return;

    if(length >= (int)sizeof(line))
        length = (int)sizeof(line) - 1;

    (void)app_uart4_console_write((const uint8_t *)line, (uint16_t)length);
}

static void app_ap6212_print_ip(const char *prefix, uint32_t ip)
{
    probe_printf("%s%lu.%lu.%lu.%lu\r\n",
                 prefix,
                 (unsigned long)((ip >> 24) & 0xFFU),
                 (unsigned long)((ip >> 16) & 0xFFU),
                 (unsigned long)((ip >> 8) & 0xFFU),
                 (unsigned long)(ip & 0xFFU));
}

#define APP_AP6212_CORE_SDIO_DEV             0x0829U
#define APP_AP6212_SDPCM_PROT_VERSION        4U
#define APP_AP6212_SDPCM_TOSBMAILBOX         0x00000040U
#define APP_AP6212_SDPCM_TOHOSTMAILBOXDATA   0x0000004CU
#define APP_AP6212_SDPCM_TOSBMAILBOXDATA     0x00000048U
#define APP_AP6212_SDPCM_INTSTATUS           0x00000020U
#define APP_AP6212_SDPCM_HOSTINTMASK         0x00000024U
#define APP_AP6212_SMB_INT_ACK               0x00000002U
#define APP_AP6212_HMB_DATA_DEVREADY         0x00000002U
#define APP_AP6212_HMB_DATA_FWREADY          0x00000008U
#define APP_AP6212_HMB_DATA_VERSION_MASK     0x00FF0000U
#define APP_AP6212_HMB_DATA_VERSION_SHIFT    16U
#define APP_AP6212_I_HMB_HOST_INT            0x00000080U
#define APP_AP6212_I_HMB_FRAME_IND           0x00000040U
#define APP_AP6212_I_CHIPACTIVE              0x20000000U
#define APP_AP6212_HOSTINTMASK               (APP_AP6212_I_HMB_HOST_INT | \
                                              APP_AP6212_I_HMB_FRAME_IND | \
                                              APP_AP6212_I_CHIPACTIVE)

#define APP_AP6212_SDPCM_HWHDR_LEN           4U
#define APP_AP6212_SDPCM_SWHDR_LEN           8U
#define APP_AP6212_SDPCM_HDR_LEN             (APP_AP6212_SDPCM_HWHDR_LEN + \
                                              APP_AP6212_SDPCM_SWHDR_LEN)
#define APP_AP6212_SDPCM_FIRST_READ          64U
#define APP_AP6212_SDPCM_CONTROL_CHANNEL     0U
#define APP_AP6212_SDPCM_EVENT_CHANNEL       1U
#define APP_AP6212_SDPCM_DATA_CHANNEL        2U
#define APP_AP6212_SDPCM_CHANNEL_SHIFT       8U
#define APP_AP6212_SDPCM_NEXTLEN_SHIFT       16U
#define APP_AP6212_SDPCM_DOFFSET_SHIFT       24U

#define APP_AP6212_BCDC_DCMD_LEN             16U
#define APP_AP6212_BCDC_DCMD_ERROR           0x01U
#define APP_AP6212_BCDC_DCMD_SET             0x02U
#define APP_AP6212_BCDC_DCMD_ID_SHIFT        16U
#define APP_AP6212_BCDC_DCMD_ID_MASK         0xFFFF0000U
#define APP_AP6212_BCDC_DCMD_IF_SHIFT        12U
#define APP_AP6212_BCDC_DCMD_IF_MASK         0x0000F000U
#define APP_AP6212_BRCMF_C_GET_VERSION       1U
#define APP_AP6212_BRCMF_C_UP                2U
#define APP_AP6212_BRCMF_C_SET_INFRA         20U
#define APP_AP6212_BRCMF_C_GET_BSSID         23U
#define APP_AP6212_BRCMF_C_GET_SSID          25U
#define APP_AP6212_BRCMF_C_SET_SSID          26U
#define APP_AP6212_BRCMF_C_GET_VAR           262U
#define APP_AP6212_BRCMF_C_SET_VAR           263U
#define APP_AP6212_BRCMF_C_SET_WSEC_PMK      268U

#define APP_AP6212_WSEC_PASSPHRASE           0x0001U
#define APP_AP6212_TKIP_ENABLED              0x0002U
#define APP_AP6212_AES_ENABLED               0x0004U
#define APP_AP6212_WPA_AUTH_PSK              0x0004U
#define APP_AP6212_WPA2_AUTH_PSK             0x0080U

#define APP_AP6212_EVENT_SET_SSID            0U
#define APP_AP6212_EVENT_LINK                16U
#define APP_AP6212_EVENT_PSK_SUP             46U
#define APP_AP6212_EVENT_IF                  54U
#define APP_AP6212_EVENT_MSG_LINK            0x0001U
#define APP_AP6212_EVENT_STATUS_SUCCESS      0U
#define APP_AP6212_EVENT_STATUS_FWSUP_COMPLETED 6U
#define APP_AP6212_EVENT_MASK_LEN            16U

typedef struct
{
    uint16_t length;
    uint16_t checksum;
    uint32_t swheader;
    uint32_t swheader2;
    uint8_t seq;
    uint8_t channel;
    uint8_t next_length;
    uint8_t data_offset;
} app_ap6212_sdpcm_header_t;

static int app_ap6212_find_core_base(const bsp_ap6212_sdio_probe_t *probe,
                                     uint16_t core_id,
                                     uint32_t *base)
{
    if(probe == NULL || base == NULL)
        return -1;

    for(uint8_t i = 0U; i < probe->core_count; i++)
    {
        if(probe->core_id[i] == core_id)
        {
            *base = probe->core_base[i];
            return 0;
        }
    }

    return -2;
}

static int app_ap6212_backplane_read32(uint32_t address, uint32_t *value)
{
    uint8_t bytes[4];

    if(value == NULL)
        return -1;
    if(bsp_ap6212_sdio_ram_read(address, bytes, sizeof(bytes)) != 0)
        return -2;

    *value = probe_get_u32_le(bytes);
    return 0;
}

static int app_ap6212_backplane_write32(uint32_t address, uint32_t value)
{
    uint8_t bytes[4];

    probe_put_u32_le(bytes, value);
    return bsp_ap6212_sdio_ram_write(address, bytes, sizeof(bytes));
}

static int app_ap6212_read_sdpcm_shared(const bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t shared_ptr = 0U;
    uint32_t shared_tail;
    uint32_t flags;
    uint32_t trap_addr;
    uint32_t console_addr;
    uint32_t msgtrace_addr;
    uint32_t brpt_addr;
    int status;

    if(probe == NULL || probe->socram_ram_size < 64U)
        return -1;

    shared_tail = probe->socram_ram_base + probe->socram_ram_size - 4U;
    status = app_ap6212_backplane_read32(shared_tail, &shared_ptr);
    if(status != 0)
        return -2;

    if(shared_ptr == 0U ||
       ((((~shared_ptr) >> 16) & 0xFFFFU) == (shared_ptr & 0xFFFFU)))
    {
        probe_printf("[sdpcm] shared pointer invalid ptr=0x%08lX tail=0x%08lX\r\n",
                     (unsigned long)shared_ptr,
                     (unsigned long)shared_tail);
        return -3;
    }

    status = bsp_ap6212_sdio_ram_read(shared_ptr,
                                      g_ap6212_shared_buffer,
                                      sizeof(g_ap6212_shared_buffer));
    if(status != 0)
        return -4;

    flags = probe_get_u32_le(&g_ap6212_shared_buffer[0]);
    trap_addr = probe_get_u32_le(&g_ap6212_shared_buffer[4]);
    console_addr = probe_get_u32_le(&g_ap6212_shared_buffer[20]);
    msgtrace_addr = probe_get_u32_le(&g_ap6212_shared_buffer[24]);
    brpt_addr = probe_get_u32_le(&g_ap6212_shared_buffer[60]);

    probe_printf("[sdpcm] shared ptr=0x%08lX flags=0x%08lX trap=0x%08lX console=0x%08lX msgtrace=0x%08lX brpt=0x%08lX tag=%.32s\r\n",
                 (unsigned long)shared_ptr,
                 (unsigned long)flags,
                 (unsigned long)trap_addr,
                 (unsigned long)console_addr,
                 (unsigned long)msgtrace_addr,
                 (unsigned long)brpt_addr,
                 (const char *)&g_ap6212_shared_buffer[28]);

    return 0;
}

static int app_ap6212_f2_firstread_smoke(void)
{
    uint32_t last_sta = 0U;
    uint16_t length;
    uint16_t checksum;
    uint32_t swheader;
    uint32_t swheader2;
    uint32_t channel;
    uint32_t data_offset;
    uint32_t next_length;
    int status;

    memset(g_ap6212_shared_buffer, 0, sizeof(g_ap6212_shared_buffer));
    status = bsp_ap6212_sdio_read_ext(2U,
                                      0U,
                                      0U,
                                      g_ap6212_shared_buffer,
                                      sizeof(g_ap6212_shared_buffer),
                                      &last_sta);
    if(status != 0)
    {
        bsp_ap6212_sdio_ext_debug_t debug;
        bsp_ap6212_sdio_get_last_ext_debug(&debug);
        probe_printf("[sdpcm] f2 firstread failed status=%ld sta=0x%08lX resp=0x%08lX xfer=%lu/%lu\r\n",
                     (long)status,
                     (unsigned long)debug.sta,
                     (unsigned long)debug.resp1,
                     (unsigned long)debug.transferred,
                     (unsigned long)debug.length);
        return -1;
    }

    length = (uint16_t)g_ap6212_shared_buffer[0] |
             ((uint16_t)g_ap6212_shared_buffer[1] << 8);
    checksum = (uint16_t)g_ap6212_shared_buffer[2] |
               ((uint16_t)g_ap6212_shared_buffer[3] << 8);
    swheader = probe_get_u32_le(&g_ap6212_shared_buffer[4]);
    swheader2 = probe_get_u32_le(&g_ap6212_shared_buffer[8]);
    channel = (swheader >> 8) & 0x0FU;
    next_length = (swheader >> 16) & 0xFFU;
    data_offset = (swheader >> 24) & 0xFFU;

    probe_printf("[sdpcm] f2 firstread status=0 sta=0x%08lX len=%u chk=0x%04X sw=0x%08lX sw2=0x%08lX ch=%lu doff=%lu next=%lu bytes=%02X %02X %02X %02X\r\n",
                 (unsigned long)last_sta,
                 (unsigned int)length,
                 (unsigned int)checksum,
                 (unsigned long)swheader,
                 (unsigned long)swheader2,
                 (unsigned long)channel,
                 (unsigned long)data_offset,
                 (unsigned long)next_length,
                 (unsigned int)g_ap6212_shared_buffer[12],
                 (unsigned int)g_ap6212_shared_buffer[13],
                 (unsigned int)g_ap6212_shared_buffer[14],
                 (unsigned int)g_ap6212_shared_buffer[15]);

    return ((((uint16_t)(~(length ^ checksum))) == 0U) && length >= 12U) ? 0 : -2;
}

static uint16_t app_ap6212_round_up4(uint16_t value)
{
    return (uint16_t)((value + 3U) & ~3U);
}

static int app_ap6212_parse_sdpcm_header(const uint8_t *frame,
                                         app_ap6212_sdpcm_header_t *header)
{
    if(frame == NULL || header == NULL)
        return -1;

    header->length = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
    header->checksum = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
    header->swheader = probe_get_u32_le(&frame[4]);
    header->swheader2 = probe_get_u32_le(&frame[8]);
    header->seq = (uint8_t)(header->swheader & 0xFFU);
    header->channel = (uint8_t)((header->swheader >>
                                 APP_AP6212_SDPCM_CHANNEL_SHIFT) & 0x0FU);
    header->next_length = (uint8_t)((header->swheader >>
                                     APP_AP6212_SDPCM_NEXTLEN_SHIFT) & 0xFFU);
    header->data_offset = (uint8_t)((header->swheader >>
                                     APP_AP6212_SDPCM_DOFFSET_SHIFT) & 0xFFU);

    if((header->length | header->checksum) == 0U)
        return -2;
    if(((uint16_t)(~(header->length ^ header->checksum))) != 0U)
        return -3;
    if(header->length < APP_AP6212_SDPCM_HDR_LEN ||
       header->data_offset < APP_AP6212_SDPCM_HDR_LEN ||
       header->data_offset > header->length)
    {
        return -4;
    }

    return 0;
}

static void app_ap6212_log_last_ext_failure(const char *tag, int status)
{
    bsp_ap6212_sdio_ext_debug_t debug;

    bsp_ap6212_sdio_get_last_ext_debug(&debug);
    probe_printf("%s status=%ld sta=0x%08lX resp=0x%08lX xfer=%lu/%lu addr=0x%05lX wr=%lu fn=%lu inc=%lu\r\n",
                 tag,
                 (long)status,
                 (unsigned long)debug.sta,
                 (unsigned long)debug.resp1,
                 (unsigned long)debug.transferred,
                 (unsigned long)debug.length,
                 (unsigned long)debug.address,
                 (unsigned long)debug.write,
                 (unsigned long)debug.function,
                 (unsigned long)debug.increment);
}

static int app_ap6212_write_f2_stream(const uint8_t *data,
                                      uint16_t length,
                                      uint32_t *last_sta)
{
    uint16_t offset = 0U;
    uint32_t sta = 0U;
    int status;

    if(last_sta != NULL)
        *last_sta = 0U;

    while(offset < length)
    {
        uint16_t chunk = (uint16_t)(length - offset);

        if(chunk > 512U)
            chunk = 512U;

        status = bsp_ap6212_sdio_write_ext(2U,
                                           0U,
                                           0U,
                                           &data[offset],
                                           chunk,
                                           &sta);
        if(last_sta != NULL)
            *last_sta = sta;
        if(status != 0)
            return status;

        offset = (uint16_t)(offset + chunk);
    }

    return 0;
}

static int app_ap6212_sdpcm_clear_pending(uint32_t sdio_base)
{
    uint32_t intstatus = 0U;
    uint32_t hmb_data = 0U;
    uint32_t masked;
    int status;

    status = app_ap6212_backplane_read32(sdio_base +
                                         APP_AP6212_SDPCM_INTSTATUS,
                                         &intstatus);
    if(status != 0)
        return -1;

    masked = intstatus & APP_AP6212_HOSTINTMASK;
    if(masked == 0U)
        return 0;

    (void)app_ap6212_backplane_write32(sdio_base +
                                       APP_AP6212_SDPCM_INTSTATUS,
                                       masked);

    if((masked & APP_AP6212_I_HMB_HOST_INT) != 0U)
    {
        (void)app_ap6212_backplane_read32(sdio_base +
                                          APP_AP6212_SDPCM_TOHOSTMAILBOXDATA,
                                          &hmb_data);
        (void)app_ap6212_backplane_write32(sdio_base +
                                           APP_AP6212_SDPCM_TOSBMAILBOX,
                                           APP_AP6212_SMB_INT_ACK);
    }

    probe_printf("[sdpcm] clear pending intstatus=0x%08lX masked=0x%08lX hmb=0x%08lX\r\n",
                 (unsigned long)intstatus,
                 (unsigned long)masked,
                 (unsigned long)hmb_data);

    return 0;
}

static int app_ap6212_sdpcm_write_control(const uint8_t *payload,
                                          uint16_t payload_length)
{
    uint16_t frame_length;
    uint16_t send_length;
    uint16_t checksum;
    uint32_t swheader;
    uint32_t last_sta = 0U;
    int status;

    if(payload == NULL || payload_length == 0U)
        return -1;

    frame_length = APP_AP6212_SDPCM_HDR_LEN + payload_length;
    send_length = app_ap6212_round_up4(frame_length);
    if(send_length > sizeof(g_ap6212_sdpcm_tx))
        return -2;

    g_ap6212_sdpcm_readahead = 0U;
    memset(g_ap6212_sdpcm_tx, 0, send_length);
    memcpy(&g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_HDR_LEN],
           payload,
           payload_length);

    checksum = (uint16_t)(frame_length ^ 0xFFFFU);
    g_ap6212_sdpcm_tx[0] = (uint8_t)frame_length;
    g_ap6212_sdpcm_tx[1] = (uint8_t)(frame_length >> 8);
    g_ap6212_sdpcm_tx[2] = (uint8_t)checksum;
    g_ap6212_sdpcm_tx[3] = (uint8_t)(checksum >> 8);
    swheader = ((uint32_t)g_ap6212_sdpcm_tx_seq) |
               ((uint32_t)APP_AP6212_SDPCM_CONTROL_CHANNEL <<
                APP_AP6212_SDPCM_CHANNEL_SHIFT) |
               ((uint32_t)APP_AP6212_SDPCM_HDR_LEN <<
                APP_AP6212_SDPCM_DOFFSET_SHIFT);
    probe_put_u32_le(&g_ap6212_sdpcm_tx[4], swheader);
    probe_put_u32_le(&g_ap6212_sdpcm_tx[8], 0U);

    status = app_ap6212_write_f2_stream(g_ap6212_sdpcm_tx,
                                        send_length,
                                        &last_sta);
    if(status != 0)
    {
        app_ap6212_log_last_ext_failure("[sdpcm] txctl failed", status);
        return -3;
    }

    probe_printf("[sdpcm] txctl seq=%u frame=%u send=%u payload=%u sta=0x%08lX\r\n",
                 (unsigned int)g_ap6212_sdpcm_tx_seq,
                 (unsigned int)frame_length,
                 (unsigned int)send_length,
                 (unsigned int)payload_length,
                 (unsigned long)last_sta);

    g_ap6212_sdpcm_tx_seq = (uint8_t)(g_ap6212_sdpcm_tx_seq + 1U);
    return 0;
}

#if APP_AP6212_NET_TRACE
static uint8_t app_ap6212_dhcp_message_type(const uint8_t *dhcp,
                                            uint16_t dhcp_length)
{
    uint16_t pos;

    if(dhcp_length < 241U || probe_get_u32_be(&dhcp[236]) != 0x63825363U)
        return 0U;

    pos = 240U;
    while(pos < dhcp_length)
    {
        uint8_t code = dhcp[pos++];
        uint8_t len;

        if(code == 0U)
            continue;
        if(code == 255U)
            break;
        if(pos >= dhcp_length)
            break;

        len = dhcp[pos++];
        if(pos + len > dhcp_length)
            break;
        if(code == 53U && len == 1U)
            return dhcp[pos];

        pos = (uint16_t)(pos + len);
    }

    return 0U;
}

static void app_ap6212_trace_dhcp_wire(const char *tag,
                                       const uint8_t *ethernet,
                                       uint16_t ethernet_length)
{
    const uint8_t *ip;
    const uint8_t *udp;
    const uint8_t *dhcp;
    uint16_t ip_header_len;
    uint16_t udp_length;
    uint16_t dhcp_length;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t msg_type;

    if(ethernet_length < 282U ||
       probe_get_u16_be(&ethernet[12]) != 0x0800U)
    {
        return;
    }

    ip = &ethernet[14];
    ip_header_len = (uint16_t)((ip[0] & 0x0FU) * 4U);
    if(ip_header_len < 20U ||
       ethernet_length < (uint16_t)(14U + ip_header_len + 8U) ||
       ip[9] != 17U)
    {
        return;
    }

    udp = &ethernet[14U + ip_header_len];
    src_port = probe_get_u16_be(&udp[0]);
    dst_port = probe_get_u16_be(&udp[2]);
    if(!((src_port == 67U && dst_port == 68U) ||
         (src_port == 68U && dst_port == 67U)))
    {
        return;
    }

    udp_length = probe_get_u16_be(&udp[4]);
    if(udp_length < 8U ||
       ethernet_length < (uint16_t)(14U + ip_header_len + udp_length))
    {
        return;
    }

    dhcp = &udp[8];
    dhcp_length = (uint16_t)(udp_length - 8U);
    msg_type = app_ap6212_dhcp_message_type(dhcp, dhcp_length);

    probe_printf("[%s] msg=%u xid=0x%08lX flags=0x%04X yi=%lu.%lu.%lu.%lu ch=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 tag,
                 (unsigned int)msg_type,
                 (unsigned long)probe_get_u32_be(&dhcp[4]),
                 (unsigned int)probe_get_u16_be(&dhcp[10]),
                 (unsigned long)dhcp[16],
                 (unsigned long)dhcp[17],
                 (unsigned long)dhcp[18],
                 (unsigned long)dhcp[19],
                 (unsigned int)dhcp[28],
                 (unsigned int)dhcp[29],
                 (unsigned int)dhcp[30],
                 (unsigned int)dhcp[31],
                 (unsigned int)dhcp[32],
                 (unsigned int)dhcp[33]);
}
#endif /* APP_AP6212_NET_TRACE */

static int app_ap6212_sdpcm_write_data(const uint8_t *ethernet,
                                       uint16_t ethernet_length)
{
    uint16_t payload_length;
    uint16_t unpadded_length;
    uint16_t frame_length;
    uint16_t send_length;
    uint16_t checksum;
    uint32_t swheader;
    uint32_t last_sta = 0U;
    int status;

    if(ethernet == NULL || ethernet_length < 14U)
        return -1;

#if APP_AP6212_NET_TRACE
    if(probe_get_u16_be(&ethernet[12]) == 0x0800U && ethernet_length >= 42U)
    {
        const uint8_t *ip = &ethernet[14];
        uint16_t ip_header_len = (uint16_t)((ip[0] & 0x0FU) * 4U);

        if(ip_header_len >= 20U &&
           ethernet_length >= (uint16_t)(14U + ip_header_len + 8U) &&
           ip[9] == 17U)
        {
            const uint8_t *udp = &ethernet[14U + ip_header_len];

            probe_printf("[net-tx] len=%u dst=%02X:%02X:%02X:%02X:%02X:%02X ip=%lu.%lu.%lu.%lu>%lu.%lu.%lu.%lu udp=%u>%u\r\n",
                         (unsigned int)ethernet_length,
                         (unsigned int)ethernet[0],
                         (unsigned int)ethernet[1],
                         (unsigned int)ethernet[2],
                         (unsigned int)ethernet[3],
                         (unsigned int)ethernet[4],
                         (unsigned int)ethernet[5],
                         (unsigned long)ip[12],
                         (unsigned long)ip[13],
                         (unsigned long)ip[14],
                         (unsigned long)ip[15],
                         (unsigned long)ip[16],
                         (unsigned long)ip[17],
                         (unsigned long)ip[18],
                         (unsigned long)ip[19],
                         (unsigned int)probe_get_u16_be(&udp[0]),
                         (unsigned int)probe_get_u16_be(&udp[2]));
        }
    }
    app_ap6212_trace_dhcp_wire("dhcp-tx", ethernet, ethernet_length);
#endif /* APP_AP6212_NET_TRACE */

    payload_length = (uint16_t)(4U + ethernet_length);
    unpadded_length = APP_AP6212_SDPCM_HDR_LEN + payload_length;
    send_length = (uint16_t)((unpadded_length + 63U) & ~63U);
    frame_length = send_length;
    if(send_length > sizeof(g_ap6212_sdpcm_tx))
        return -2;

    g_ap6212_sdpcm_readahead = 0U;
    memset(g_ap6212_sdpcm_tx, 0, send_length);
    g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_HDR_LEN] = 0x20U;
    g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_HDR_LEN + 1U] = 0U;
    g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_HDR_LEN + 2U] = 0U;
    g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_HDR_LEN + 3U] = 0U;
    memcpy(&g_ap6212_sdpcm_tx[APP_AP6212_SDPCM_HDR_LEN + 4U],
           ethernet,
           ethernet_length);

    checksum = (uint16_t)(frame_length ^ 0xFFFFU);
    g_ap6212_sdpcm_tx[0] = (uint8_t)frame_length;
    g_ap6212_sdpcm_tx[1] = (uint8_t)(frame_length >> 8);
    g_ap6212_sdpcm_tx[2] = (uint8_t)checksum;
    g_ap6212_sdpcm_tx[3] = (uint8_t)(checksum >> 8);
    swheader = ((uint32_t)g_ap6212_sdpcm_tx_seq) |
               ((uint32_t)APP_AP6212_SDPCM_DATA_CHANNEL <<
                APP_AP6212_SDPCM_CHANNEL_SHIFT) |
               ((uint32_t)APP_AP6212_SDPCM_HDR_LEN <<
                APP_AP6212_SDPCM_DOFFSET_SHIFT);
    probe_put_u32_le(&g_ap6212_sdpcm_tx[4], swheader);
    probe_put_u32_le(&g_ap6212_sdpcm_tx[8], 0U);

    status = app_ap6212_write_f2_stream(g_ap6212_sdpcm_tx,
                                        send_length,
                                        &last_sta);
    if(status != 0)
    {
        app_ap6212_log_last_ext_failure("[sdpcm] txdata failed", status);
        return -3;
    }

    probe_printf("[sdpcm] txdata seq=%u frame=%u send=%u eth=%u sta=0x%08lX\r\n",
                 (unsigned int)g_ap6212_sdpcm_tx_seq,
                 (unsigned int)frame_length,
                 (unsigned int)send_length,
                 (unsigned int)ethernet_length,
                 (unsigned long)last_sta);

    g_ap6212_sdpcm_tx_seq = (uint8_t)(g_ap6212_sdpcm_tx_seq + 1U);
    return 0;
}

static int app_ap6212_sdpcm_read_frame(uint32_t sdio_base,
                                       uint8_t *payload,
                                       uint16_t payload_capacity,
                                       uint16_t *payload_length,
                                       uint8_t *channel,
                                       uint32_t timeout_ticks)
{
    app_ap6212_sdpcm_header_t header;
    uint32_t intstatus = 0U;
    uint32_t hmb_data = 0U;
    uint32_t masked;
    uint32_t last_sta = 0U;
    uint16_t remaining;
    uint16_t read_remaining;
    uint16_t read_offset;
    uint16_t read_chunk;
    uint8_t direct_read;
    int status;

    if(payload == NULL || payload_length == NULL || channel == NULL)
        return -1;

    for(uint32_t retry = 0U; retry < timeout_ticks; retry++)
    {
        direct_read = g_ap6212_sdpcm_readahead;
        if(direct_read != 0U)
        {
            g_ap6212_sdpcm_readahead = 0U;
            masked = APP_AP6212_I_HMB_FRAME_IND;
        }
        else
        {
            status = app_ap6212_backplane_read32(sdio_base +
                                                 APP_AP6212_SDPCM_INTSTATUS,
                                                 &intstatus);
            if(status != 0)
                return -2;

            masked = intstatus & APP_AP6212_HOSTINTMASK;
            if(masked != 0U)
            {
                (void)app_ap6212_backplane_write32(sdio_base +
                                                   APP_AP6212_SDPCM_INTSTATUS,
                                                   masked);

                if((masked & APP_AP6212_I_HMB_HOST_INT) != 0U)
                {
                    (void)app_ap6212_backplane_read32(sdio_base +
                                                      APP_AP6212_SDPCM_TOHOSTMAILBOXDATA,
                                                      &hmb_data);
                    (void)app_ap6212_backplane_write32(sdio_base +
                                                       APP_AP6212_SDPCM_TOSBMAILBOX,
                                                       APP_AP6212_SMB_INT_ACK);
                    probe_printf("[sdpcm] hostmail during rx int=0x%08lX hmb=0x%08lX\r\n",
                                 (unsigned long)intstatus,
                                 (unsigned long)hmb_data);
                }
            }
        }

        if((masked & APP_AP6212_I_HMB_FRAME_IND) == 0U)
        {
            tx_thread_sleep(1U);
            continue;
        }

        memset(g_ap6212_sdpcm_rx, 0, APP_AP6212_SDPCM_FIRST_READ);
        status = bsp_ap6212_sdio_read_ext(2U,
                                          0U,
                                          0U,
                                          g_ap6212_sdpcm_rx,
                                          APP_AP6212_SDPCM_FIRST_READ,
                                          &last_sta);
        if(status != 0)
        {
            if(direct_read != 0U)
            {
                tx_thread_sleep(1U);
                continue;
            }
            app_ap6212_log_last_ext_failure("[sdpcm] rx firstread failed", status);
            return -3;
        }

        status = app_ap6212_parse_sdpcm_header(g_ap6212_sdpcm_rx, &header);
        if(status == -2)
        {
            tx_thread_sleep(1U);
            continue;
        }
        if(status != 0)
        {
            probe_printf("[sdpcm] rx bad header status=%d len=%u chk=0x%04X sw=0x%08lX sw2=0x%08lX\r\n",
                         status,
                         (unsigned int)((uint16_t)g_ap6212_sdpcm_rx[0] |
                                        ((uint16_t)g_ap6212_sdpcm_rx[1] << 8)),
                         (unsigned int)((uint16_t)g_ap6212_sdpcm_rx[2] |
                                        ((uint16_t)g_ap6212_sdpcm_rx[3] << 8)),
                         (unsigned long)probe_get_u32_le(&g_ap6212_sdpcm_rx[4]),
                         (unsigned long)probe_get_u32_le(&g_ap6212_sdpcm_rx[8]));
            return -4;
        }

        if(header.length > sizeof(g_ap6212_sdpcm_rx))
        {
            probe_printf("[sdpcm] rx too long len=%u cap=%u\r\n",
                         (unsigned int)header.length,
                         (unsigned int)sizeof(g_ap6212_sdpcm_rx));
            return -5;
        }

        if(header.length > APP_AP6212_SDPCM_FIRST_READ)
        {
            remaining = header.length - APP_AP6212_SDPCM_FIRST_READ;
            read_remaining = app_ap6212_round_up4(remaining);
            if((uint32_t)APP_AP6212_SDPCM_FIRST_READ + read_remaining >
               sizeof(g_ap6212_sdpcm_rx))
            {
                return -6;
            }

            read_offset = APP_AP6212_SDPCM_FIRST_READ;
            while(read_remaining != 0U)
            {
                read_chunk = (read_remaining > 512U) ? 512U : read_remaining;
                status = bsp_ap6212_sdio_read_ext(2U,
                                                  0U,
                                                  0U,
                                                  &g_ap6212_sdpcm_rx[read_offset],
                                                  read_chunk,
                                                  &last_sta);
                if(status != 0)
                {
                    app_ap6212_log_last_ext_failure("[sdpcm] rx body failed", status);
                    return -7;
                }

                read_offset = (uint16_t)(read_offset + read_chunk);
                read_remaining = (uint16_t)(read_remaining - read_chunk);
            }
        }

        *payload_length = header.length - header.data_offset;
        if(*payload_length > payload_capacity)
        {
            probe_printf("[sdpcm] rx payload too long len=%u cap=%u ch=%u\r\n",
                         (unsigned int)*payload_length,
                         (unsigned int)payload_capacity,
                         (unsigned int)header.channel);
            return -8;
        }

        memcpy(payload,
               &g_ap6212_sdpcm_rx[header.data_offset],
               *payload_length);
        *channel = header.channel;

        probe_printf("[sdpcm] rx frame ch=%u seq=%u len=%u doff=%u next=%u payload=%u sta=0x%08lX\r\n",
                     (unsigned int)header.channel,
                     (unsigned int)header.seq,
                     (unsigned int)header.length,
                     (unsigned int)header.data_offset,
                     (unsigned int)header.next_length,
                     (unsigned int)*payload_length,
                     (unsigned long)last_sta);

        g_ap6212_sdpcm_readahead = (header.next_length != 0U) ? 1U : 0U;
        return 0;
    }

    return -9;
}

static void app_ap6212_sanitize_ascii(uint8_t *data, uint16_t length)
{
    if(data == NULL)
        return;

    for(uint16_t i = 0U; i < length; i++)
    {
        if(data[i] == '\0')
            break;
        if(data[i] == '\r' || data[i] == '\n' || data[i] == '\t')
            data[i] = ' ';
        else if(data[i] < 0x20U || data[i] > 0x7EU)
            data[i] = '.';
    }
}

static int app_ap6212_bcdc_dcmd(uint32_t sdio_base,
                                uint32_t command,
                                uint8_t set,
                                const uint8_t *request,
                                uint16_t request_length,
                                uint16_t ioctl_length,
                                uint8_t *response,
                                uint16_t response_capacity,
                                uint16_t *response_length)
{
    uint16_t reqid;
    uint16_t rx_length = 0U;
    uint16_t rx_data_length;
    uint16_t copy_length;
    uint8_t channel = 0U;
    uint32_t flags;
    uint32_t resp_command;
    uint32_t resp_length;
    uint32_t resp_flags;
    int32_t fw_status;
    uint32_t resp_id;
    int status;

    if(ioctl_length == 0U ||
       request_length > ioctl_length ||
       APP_AP6212_BCDC_DCMD_LEN + ioctl_length > sizeof(g_ap6212_bcdc_payload) ||
       (response == NULL && response_capacity != 0U) ||
       response_length == NULL)
    {
        return -1;
    }

    memset(g_ap6212_bcdc_payload, 0, sizeof(g_ap6212_bcdc_payload));
    if(response != NULL && response_capacity != 0U)
        memset(response, 0, response_capacity);
    *response_length = 0U;

    reqid = ++g_ap6212_bcdc_reqid;
    flags = (((uint32_t)reqid << APP_AP6212_BCDC_DCMD_ID_SHIFT) &
             APP_AP6212_BCDC_DCMD_ID_MASK);
    if(set != 0U)
        flags |= APP_AP6212_BCDC_DCMD_SET;
    flags = (flags & ~APP_AP6212_BCDC_DCMD_IF_MASK) |
            (0U << APP_AP6212_BCDC_DCMD_IF_SHIFT);

    probe_put_u32_le(&g_ap6212_bcdc_payload[0], command);
    probe_put_u32_le(&g_ap6212_bcdc_payload[4], ioctl_length);
    probe_put_u32_le(&g_ap6212_bcdc_payload[8], flags);
    probe_put_u32_le(&g_ap6212_bcdc_payload[12], 0U);
    if(request != NULL && request_length != 0U)
        memcpy(&g_ap6212_bcdc_payload[APP_AP6212_BCDC_DCMD_LEN],
               request,
               request_length);

    probe_printf("[bcdc] %s tx cmd=%lu req=%u ioctl_len=%u request_len=%u\r\n",
                 set != 0U ? "set" : "query",
                 (unsigned long)command,
                 (unsigned int)reqid,
                 (unsigned int)ioctl_length,
                 (unsigned int)request_length);

    status = app_ap6212_sdpcm_write_control(g_ap6212_bcdc_payload,
                                            APP_AP6212_BCDC_DCMD_LEN +
                                            ioctl_length);
    if(status != 0)
        return -2;

    for(uint32_t attempt = 0U; attempt < 30U; attempt++)
    {
        status = app_ap6212_sdpcm_read_frame(sdio_base,
                                             g_ap6212_bcdc_payload,
                                             sizeof(g_ap6212_bcdc_payload),
                                             &rx_length,
                                             &channel,
                                             100U);
        if(status != 0)
        {
            if(status == -9)
                continue;

            probe_printf("[bcdc] %s rx failed cmd=%lu req=%u status=%d\r\n",
                         set != 0U ? "set" : "query",
                         (unsigned long)command,
                         (unsigned int)reqid,
                         status);
            return -3;
        }

        if(channel != APP_AP6212_SDPCM_CONTROL_CHANNEL)
        {
            probe_printf("[bcdc] skip non-control ch=%u len=%u b0=%02X b1=%02X b2=%02X b3=%02X\r\n",
                         (unsigned int)channel,
                         (unsigned int)rx_length,
                         (unsigned int)g_ap6212_bcdc_payload[0],
                         (unsigned int)g_ap6212_bcdc_payload[1],
                         (unsigned int)g_ap6212_bcdc_payload[2],
                         (unsigned int)g_ap6212_bcdc_payload[3]);
            g_ap6212_sdpcm_readahead = 1U;
            continue;
        }

        if(rx_length < APP_AP6212_BCDC_DCMD_LEN)
            return -4;

        resp_command = probe_get_u32_le(&g_ap6212_bcdc_payload[0]);
        resp_length = probe_get_u32_le(&g_ap6212_bcdc_payload[4]);
        resp_flags = probe_get_u32_le(&g_ap6212_bcdc_payload[8]);
        fw_status = (int32_t)probe_get_u32_le(&g_ap6212_bcdc_payload[12]);
        resp_id = (resp_flags & APP_AP6212_BCDC_DCMD_ID_MASK) >>
                  APP_AP6212_BCDC_DCMD_ID_SHIFT;

        probe_printf("[bcdc] %s rx cmd=%lu req=%lu expected=%u len=%lu flags=0x%08lX fw_status=%ld\r\n",
                     set != 0U ? "set" : "query",
                     (unsigned long)resp_command,
                     (unsigned long)resp_id,
                     (unsigned int)reqid,
                     (unsigned long)resp_length,
                     (unsigned long)resp_flags,
                     (long)fw_status);

        if(resp_id != reqid)
            continue;
        if(resp_command != command)
            return -5;
        if((resp_flags & APP_AP6212_BCDC_DCMD_ERROR) != 0U)
            return -6;

        rx_data_length = rx_length - APP_AP6212_BCDC_DCMD_LEN;
        copy_length = rx_data_length;
        if(copy_length > response_capacity)
            copy_length = response_capacity;
        if(response != NULL && copy_length != 0U)
            memcpy(response,
                   &g_ap6212_bcdc_payload[APP_AP6212_BCDC_DCMD_LEN],
                   copy_length);
        *response_length = copy_length;

        return 0;
    }

    return -7;
}

static int app_ap6212_bcdc_set_command_u32(uint32_t sdio_base,
                                           uint32_t command,
                                           uint32_t value)
{
    uint16_t response_length = 0U;
    int status;

    probe_put_u32_le(g_ap6212_bcdc_data, value);
    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  command,
                                  1U,
                                  g_ap6212_bcdc_data,
                                  4U,
                                  4U,
                                  NULL,
                                  0U,
                                  &response_length);
    probe_printf("[wifi-cfg] set cmd=%lu value=0x%08lX status=%d\r\n",
                 (unsigned long)command,
                 (unsigned long)value,
                 status);
    return status;
}

static int app_ap6212_bcdc_set_iovar_u32(uint32_t sdio_base,
                                         const char *name,
                                         uint32_t value)
{
    uint16_t response_length = 0U;
    uint16_t name_length;
    uint16_t request_length;
    int status;

    if(name == NULL)
        return -1;

    name_length = (uint16_t)strlen(name) + 1U;
    request_length = name_length + 4U;
    if(request_length > sizeof(g_ap6212_bcdc_data))
        return -2;

    memset(g_ap6212_bcdc_data, 0, request_length);
    memcpy(g_ap6212_bcdc_data, name, name_length);
    probe_put_u32_le(&g_ap6212_bcdc_data[name_length], value);

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_SET_VAR,
                                  1U,
                                  g_ap6212_bcdc_data,
                                  request_length,
                                  request_length,
                                  NULL,
                                  0U,
                                  &response_length);
    probe_printf("[wifi-cfg] set %s=0x%08lX status=%d\r\n",
                 name,
                 (unsigned long)value,
                 status);
    return status;
}

static void app_ap6212_event_mask_set(uint8_t *mask, uint32_t event_code)
{
    uint32_t byte_index = event_code >> 3;
    uint8_t bit = (uint8_t)(event_code & 0x07U);

    if(mask == NULL || byte_index >= APP_AP6212_EVENT_MASK_LEN)
        return;

    mask[byte_index] |= (uint8_t)(1U << bit);
}

static int app_ap6212_bcdc_enable_events(uint32_t sdio_base)
{
    static const uint8_t event_msgs_name[] = "event_msgs";
    uint8_t mask[APP_AP6212_EVENT_MASK_LEN];
    uint16_t response_length = 0U;
    uint16_t name_length = (uint16_t)sizeof(event_msgs_name);
    uint16_t request_length;
    int status;

    memset(mask, 0, sizeof(mask));
    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_GET_VAR,
                                  0U,
                                  event_msgs_name,
                                  name_length,
                                  (uint16_t)(name_length + sizeof(mask)),
                                  mask,
                                  sizeof(mask),
                                  &response_length);
    if(status != 0)
    {
        probe_printf("[wifi-cfg] event_msgs get failed status=%d, use fresh mask\r\n",
                     status);
        memset(mask, 0, sizeof(mask));
    }

    app_ap6212_event_mask_set(mask, APP_AP6212_EVENT_SET_SSID);
    app_ap6212_event_mask_set(mask, APP_AP6212_EVENT_LINK);
    app_ap6212_event_mask_set(mask, APP_AP6212_EVENT_PSK_SUP);
    app_ap6212_event_mask_set(mask, APP_AP6212_EVENT_IF);

    request_length = (uint16_t)(name_length + sizeof(mask));
    if(request_length > sizeof(g_ap6212_bcdc_data))
        return -1;

    memset(g_ap6212_bcdc_data, 0, request_length);
    memcpy(g_ap6212_bcdc_data, event_msgs_name, name_length);
    memcpy(&g_ap6212_bcdc_data[name_length], mask, sizeof(mask));

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_SET_VAR,
                                  1U,
                                  g_ap6212_bcdc_data,
                                  request_length,
                                  request_length,
                                  NULL,
                                  0U,
                                  &response_length);
    probe_printf("[wifi-cfg] event mask set status=%d mask0=0x%02X mask2=0x%02X mask5=0x%02X mask6=0x%02X\r\n",
                 status,
                 (unsigned int)mask[0],
                 (unsigned int)mask[2],
                 (unsigned int)mask[5],
                 (unsigned int)mask[6]);
    return status;
}

static int app_ap6212_bcdc_set_passphrase(uint32_t sdio_base,
                                          const char *password)
{
    uint16_t response_length = 0U;
    uint16_t password_length;
    int status;

    if(password == NULL)
        return -1;

    password_length = (uint16_t)strlen(password);
    if(password_length > 128U)
        return -2;

    memset(g_ap6212_bcdc_data, 0, 132U);
    g_ap6212_bcdc_data[0] = (uint8_t)password_length;
    g_ap6212_bcdc_data[1] = (uint8_t)(password_length >> 8);
    g_ap6212_bcdc_data[2] = (uint8_t)APP_AP6212_WSEC_PASSPHRASE;
    g_ap6212_bcdc_data[3] = (uint8_t)(APP_AP6212_WSEC_PASSPHRASE >> 8);
    if(password_length != 0U)
        memcpy(&g_ap6212_bcdc_data[4], password, password_length);

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_SET_WSEC_PMK,
                                  1U,
                                  g_ap6212_bcdc_data,
                                  132U,
                                  132U,
                                  NULL,
                                  0U,
                                  &response_length);
    probe_printf("[wifi-cfg] set passphrase len=%u status=%d\r\n",
                 (unsigned int)password_length,
                 status);
    return status;
}

static int app_ap6212_bcdc_set_ssid(uint32_t sdio_base, const char *ssid)
{
    uint16_t response_length = 0U;
    uint16_t ssid_length;
    int status;

    if(ssid == NULL)
        return -1;

    ssid_length = (uint16_t)strlen(ssid);
    if(ssid_length == 0U || ssid_length > 32U)
        return -2;

    memset(g_ap6212_bcdc_data, 0, 36U);
    probe_put_u32_le(&g_ap6212_bcdc_data[0], ssid_length);
    memcpy(&g_ap6212_bcdc_data[4], ssid, ssid_length);

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_SET_SSID,
                                  1U,
                                  g_ap6212_bcdc_data,
                                  36U,
                                  36U,
                                  NULL,
                                  0U,
                                  &response_length);
    probe_printf("[wifi-cfg] set ssid=%s len=%u status=%d\r\n",
                 ssid,
                 (unsigned int)ssid_length,
                 status);
    return status;
}

static int app_ap6212_parse_wifi_event(const uint8_t *payload,
                                       uint16_t length,
                                       uint8_t *link_up)
{
    const uint8_t *event_payload = payload;
    uint16_t event_length = length;
    uint16_t event_offset = 0U;
    uint16_t flags;
    uint32_t event_code;
    uint32_t status;
    uint32_t reason;
    uint32_t datalen;
    uint8_t ifidx;
    uint8_t bsscfgidx;

    if(link_up == NULL)
        return -1;
    if(payload == NULL)
        return -1;

    if(length >= 4U && (payload[0] & 0xF0U) == 0x20U)
    {
        event_offset = (uint16_t)(4U + ((uint16_t)payload[3] << 2));
        if(event_offset < length)
        {
            event_payload = &payload[event_offset];
            event_length = length - event_offset;
        }
    }

    if(event_length < 72U)
    {
        probe_printf("[wifi-event] short len=%u offset=%u bcdc=%02X %02X %02X %02X\r\n",
                     (unsigned int)length,
                     (unsigned int)event_offset,
                     (unsigned int)payload[0],
                     (unsigned int)payload[1],
                     (unsigned int)payload[2],
                     (unsigned int)payload[3]);
        return 0;
    }

    flags = probe_get_u16_be(&event_payload[26]);
    event_code = probe_get_u32_be(&event_payload[28]);
    status = probe_get_u32_be(&event_payload[32]);
    reason = probe_get_u32_be(&event_payload[36]);
    datalen = probe_get_u32_be(&event_payload[44]);
    ifidx = event_payload[70];
    bsscfgidx = event_payload[71];

    probe_printf("[wifi-event] code=%lu status=%lu reason=%lu flags=0x%04X datalen=%lu if=%u bss=%u off=%u addr=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 (unsigned long)event_code,
                 (unsigned long)status,
                 (unsigned long)reason,
                 (unsigned int)flags,
                 (unsigned long)datalen,
                 (unsigned int)ifidx,
                 (unsigned int)bsscfgidx,
                 (unsigned int)event_offset,
                 (unsigned int)event_payload[48],
                 (unsigned int)event_payload[49],
                 (unsigned int)event_payload[50],
                 (unsigned int)event_payload[51],
                 (unsigned int)event_payload[52],
                 (unsigned int)event_payload[53]);

    if(event_code == APP_AP6212_EVENT_LINK &&
       status == APP_AP6212_EVENT_STATUS_SUCCESS &&
       (flags & APP_AP6212_EVENT_MSG_LINK) != 0U)
    {
        *link_up = 1U;
        return 1;
    }

    if(event_code == APP_AP6212_EVENT_SET_SSID &&
       status == APP_AP6212_EVENT_STATUS_SUCCESS)
    {
        *link_up = 1U;
        return 1;
    }

    if(event_code == APP_AP6212_EVENT_PSK_SUP &&
       status == APP_AP6212_EVENT_STATUS_FWSUP_COMPLETED)
    {
        return 0;
    }

    return 0;
}

static int app_ap6212_wifi_query_assoc(uint32_t sdio_base)
{
    uint16_t response_length = 0U;
    uint32_t ssid_length;
    int status;

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_GET_BSSID,
                                  0U,
                                  NULL,
                                  0U,
                                  6U,
                                  g_ap6212_bcdc_data,
                                  6U,
                                  &response_length);
    if(status != 0 || response_length < 6U)
    {
        probe_printf("[wifi] get bssid failed status=%d len=%u\r\n",
                     status,
                     (unsigned int)response_length);
        return -1;
    }

    probe_printf("[wifi] bssid=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 (unsigned int)g_ap6212_bcdc_data[0],
                 (unsigned int)g_ap6212_bcdc_data[1],
                 (unsigned int)g_ap6212_bcdc_data[2],
                 (unsigned int)g_ap6212_bcdc_data[3],
                 (unsigned int)g_ap6212_bcdc_data[4],
                 (unsigned int)g_ap6212_bcdc_data[5]);

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_GET_SSID,
                                  0U,
                                  NULL,
                                  0U,
                                  36U,
                                  g_ap6212_bcdc_data,
                                  36U,
                                  &response_length);
    if(status == 0 && response_length >= 36U)
    {
        ssid_length = probe_get_u32_le(g_ap6212_bcdc_data);
        if(ssid_length > 32U)
            ssid_length = 32U;
        g_ap6212_bcdc_data[4U + ssid_length] = '\0';
        app_ap6212_sanitize_ascii(&g_ap6212_bcdc_data[4], (uint16_t)ssid_length);
        probe_printf("[wifi] associated ssid=%s len=%lu\r\n",
                     (const char *)&g_ap6212_bcdc_data[4],
                     (unsigned long)ssid_length);
    }
    else
    {
        probe_printf("[wifi] get ssid status=%d len=%u\r\n",
                     status,
                     (unsigned int)response_length);
    }

    return 0;
}

static int app_ap6212_wifi_wait_link(uint32_t sdio_base)
{
    uint16_t rx_length = 0U;
    uint8_t channel = 0U;
    uint8_t link_up = 0U;
    int status;

    for(uint32_t attempt = 0U; attempt < 600U; attempt++)
    {
        status = app_ap6212_sdpcm_read_frame(sdio_base,
                                             g_ap6212_bcdc_payload,
                                             sizeof(g_ap6212_bcdc_payload),
                                             &rx_length,
                                             &channel,
                                             50U);
        if(status == -9)
            continue;
        if(status != 0)
        {
            probe_printf("[wifi] wait link rx status=%d\r\n", status);
            return -1;
        }

        if(channel == APP_AP6212_SDPCM_EVENT_CHANNEL)
        {
            status = app_ap6212_parse_wifi_event(g_ap6212_bcdc_payload,
                                                 rx_length,
                                                 &link_up);
            if(status > 0 || link_up != 0U)
                return 0;
        }
        else if(channel == APP_AP6212_SDPCM_DATA_CHANNEL)
        {
            probe_printf("[wifi] data before netx len=%u b0=%02X b1=%02X b2=%02X b3=%02X\r\n",
                         (unsigned int)rx_length,
                         (unsigned int)g_ap6212_bcdc_payload[0],
                         (unsigned int)g_ap6212_bcdc_payload[1],
                         (unsigned int)g_ap6212_bcdc_payload[2],
                         (unsigned int)g_ap6212_bcdc_payload[3]);
        }
        else
        {
            probe_printf("[wifi] wait link skip channel=%u len=%u\r\n",
                         (unsigned int)channel,
                         (unsigned int)rx_length);
        }
    }

    probe_write("[wifi] link wait timeout\r\n");
    return -2;
}

static int app_ap6212_read_ethernet_frame(uint32_t sdio_base,
                                          uint16_t *ethernet_length,
                                          uint32_t timeout_ticks)
{
    uint16_t rx_length = 0U;
    uint16_t offset;
    uint8_t channel = 0U;
    uint8_t link_up = 0U;
    int status;

    if(ethernet_length == NULL)
        return -1;

    for(uint32_t waited = 0U; waited < timeout_ticks; waited += 20U)
    {
        status = app_ap6212_sdpcm_read_frame(sdio_base,
                                             g_ap6212_bcdc_payload,
                                             sizeof(g_ap6212_bcdc_payload),
                                             &rx_length,
                                             &channel,
                                             20U);
        if(status == -9)
            continue;
        if(status != 0)
            return -2;

        if(channel == APP_AP6212_SDPCM_EVENT_CHANNEL)
        {
            (void)app_ap6212_parse_wifi_event(g_ap6212_bcdc_payload,
                                              rx_length,
                                              &link_up);
            continue;
        }

        if(channel != APP_AP6212_SDPCM_DATA_CHANNEL)
            continue;

        if(rx_length < 18U)
            continue;

        offset = 0U;
        if((g_ap6212_bcdc_payload[0] & 0xF0U) == 0x20U)
            offset = (uint16_t)(4U + ((uint16_t)g_ap6212_bcdc_payload[3] << 2));
        if(offset >= rx_length)
            continue;

        *ethernet_length = rx_length - offset;
        if(*ethernet_length > sizeof(g_ap6212_net_rx))
            return -3;

        memcpy(g_ap6212_net_rx,
               &g_ap6212_bcdc_payload[offset],
               *ethernet_length);
#if APP_AP6212_NET_TRACE
        probe_printf("[net-rx] len=%u type=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                     (unsigned int)*ethernet_length,
                     (unsigned int)probe_get_u16_be(&g_ap6212_net_rx[12]),
                     (unsigned int)g_ap6212_net_rx[0],
                     (unsigned int)g_ap6212_net_rx[1],
                     (unsigned int)g_ap6212_net_rx[2],
                     (unsigned int)g_ap6212_net_rx[3],
                     (unsigned int)g_ap6212_net_rx[4],
                     (unsigned int)g_ap6212_net_rx[5],
                     (unsigned int)g_ap6212_net_rx[6],
                     (unsigned int)g_ap6212_net_rx[7],
                     (unsigned int)g_ap6212_net_rx[8],
                     (unsigned int)g_ap6212_net_rx[9],
                     (unsigned int)g_ap6212_net_rx[10],
                     (unsigned int)g_ap6212_net_rx[11]);
        if(probe_get_u16_be(&g_ap6212_net_rx[12]) == 0x0800U &&
           *ethernet_length >= 42U)
        {
            const uint8_t *ip = &g_ap6212_net_rx[14];
            uint16_t ip_header_len = (uint16_t)((ip[0] & 0x0FU) * 4U);

            if(ip_header_len >= 20U &&
               *ethernet_length >= (uint16_t)(14U + ip_header_len + 8U) &&
               ip[9] == 17U)
            {
                const uint8_t *udp = &g_ap6212_net_rx[14U + ip_header_len];

                probe_printf("[net-rx-ip] ip=%lu.%lu.%lu.%lu>%lu.%lu.%lu.%lu udp=%u>%u\r\n",
                             (unsigned long)ip[12],
                             (unsigned long)ip[13],
                             (unsigned long)ip[14],
                             (unsigned long)ip[15],
                             (unsigned long)ip[16],
                             (unsigned long)ip[17],
                             (unsigned long)ip[18],
                             (unsigned long)ip[19],
                             (unsigned int)probe_get_u16_be(&udp[0]),
                             (unsigned int)probe_get_u16_be(&udp[2]));
            }
        }
        app_ap6212_trace_dhcp_wire("dhcp-rx", g_ap6212_net_rx,
                                   *ethernet_length);
#endif /* APP_AP6212_NET_TRACE */
        return 0;
    }

    return -4;
}

uint8_t app_ap6212_wifi_is_ready(void)
{
    return g_ap6212_wifi_ready;
}

int app_ap6212_wifi_get_mac(uint8_t mac[6])
{
    if(mac == NULL || g_ap6212_wifi_ready == 0U)
        return -1;

    memcpy(mac, g_ap6212_wifi_mac, 6U);
    return 0;
}

int app_ap6212_wifi_send_ethernet(const uint8_t *frame, uint16_t length)
{
    int status;

    if(frame == NULL || g_ap6212_wifi_ready == 0U)
        return -1;
    if(length < 14U || length > APP_AP6212_ETHERNET_BUFFER_SIZE)
        return -2;

    if(g_ap6212_io_mutex_ready != 0U)
        (void)tx_mutex_get(&g_ap6212_io_mutex, TX_WAIT_FOREVER);
    status = app_ap6212_sdpcm_write_data(frame, length);
    if(g_ap6212_io_mutex_ready != 0U)
        (void)tx_mutex_put(&g_ap6212_io_mutex);
    return status;
}

int app_ap6212_wifi_receive_ethernet(uint8_t *frame,
                                     uint16_t capacity,
                                     uint16_t *length,
                                     uint32_t timeout_ticks)
{
    uint16_t rx_length = 0U;
    int status;

    if(frame == NULL || length == NULL || g_ap6212_wifi_ready == 0U)
        return -1;
    if(capacity < 14U)
        return -2;

    if(g_ap6212_io_mutex_ready != 0U)
        (void)tx_mutex_get(&g_ap6212_io_mutex, TX_WAIT_FOREVER);
    status = app_ap6212_read_ethernet_frame(g_ap6212_sdio_base,
                                            &rx_length,
                                            timeout_ticks);
    if(status != 0)
    {
        if(g_ap6212_io_mutex_ready != 0U)
            (void)tx_mutex_put(&g_ap6212_io_mutex);
        return status;
    }
    if(rx_length > capacity)
    {
        if(g_ap6212_io_mutex_ready != 0U)
            (void)tx_mutex_put(&g_ap6212_io_mutex);
        return -3;
    }

    memcpy(frame, g_ap6212_net_rx, rx_length);
    *length = rx_length;
    if(g_ap6212_io_mutex_ready != 0U)
        (void)tx_mutex_put(&g_ap6212_io_mutex);
    return 0;
}

static void app_ap6212_build_ethernet_ipv4_udp(uint8_t *frame,
                                               const uint8_t *dst_mac,
                                               uint32_t src_ip,
                                               uint32_t dst_ip,
                                               uint16_t src_port,
                                               uint16_t dst_port,
                                               uint16_t udp_payload_len)
{
    uint16_t ip_total = (uint16_t)(20U + 8U + udp_payload_len);
    uint8_t *ip = &frame[14];
    uint8_t *udp = &frame[34];

    memcpy(&frame[0], dst_mac, 6U);
    memcpy(&frame[6], g_ap6212_wifi_mac, 6U);
    probe_put_u16_be(&frame[12], 0x0800U);

    memset(ip, 0, 20U);
    ip[0] = 0x45U;
    probe_put_u16_be(&ip[2], ip_total);
    probe_put_u16_be(&ip[4], 0x6212U);
    ip[8] = 64U;
    ip[9] = 17U;
    probe_put_u32_be(&ip[12], src_ip);
    probe_put_u32_be(&ip[16], dst_ip);
    probe_put_u16_be(&ip[10], app_ap6212_ip_checksum(ip, 20U));

    probe_put_u16_be(&udp[0], src_port);
    probe_put_u16_be(&udp[2], dst_port);
    probe_put_u16_be(&udp[4], (uint16_t)(8U + udp_payload_len));
    probe_put_u16_be(&udp[6], 0U);
}

static uint16_t app_ap6212_build_dhcp_frame(uint8_t message_type,
                                            uint32_t xid,
                                            uint32_t requested_ip,
                                            uint32_t server_id)
{
    static const uint8_t broadcast_mac[6] =
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
    uint8_t *dhcp = &g_ap6212_net_tx[42];
    uint16_t option = 240U;
    uint16_t dhcp_length;

    memset(g_ap6212_net_tx, 0, sizeof(g_ap6212_net_tx));
    dhcp[0] = 1U;
    dhcp[1] = 1U;
    dhcp[2] = 6U;
    probe_put_u32_be(&dhcp[4], xid);
    probe_put_u16_be(&dhcp[10], 0x8000U);
    memcpy(&dhcp[28], g_ap6212_wifi_mac, 6U);
    probe_put_u32_be(&dhcp[236], 0x63825363U);

    dhcp[option++] = 53U;
    dhcp[option++] = 1U;
    dhcp[option++] = message_type;

    if(message_type == 3U)
    {
        dhcp[option++] = 50U;
        dhcp[option++] = 4U;
        probe_put_u32_be(&dhcp[option], requested_ip);
        option = (uint16_t)(option + 4U);

        dhcp[option++] = 54U;
        dhcp[option++] = 4U;
        probe_put_u32_be(&dhcp[option], server_id);
        option = (uint16_t)(option + 4U);
    }

    dhcp[option++] = 55U;
    dhcp[option++] = 4U;
    dhcp[option++] = 1U;
    dhcp[option++] = 3U;
    dhcp[option++] = 6U;
    dhcp[option++] = 51U;
    dhcp[option++] = 255U;

    dhcp_length = (option < 300U) ? 300U : option;
    app_ap6212_build_ethernet_ipv4_udp(g_ap6212_net_tx,
                                       broadcast_mac,
                                       0U,
                                       0xFFFFFFFFU,
                                       68U,
                                       67U,
                                       dhcp_length);

    return (uint16_t)(42U + dhcp_length);
}

static int app_ap6212_parse_dhcp_frame(uint16_t ethernet_length,
                                       uint32_t xid,
                                       uint8_t expected_type)
{
    uint8_t *ip = &g_ap6212_net_rx[14];
    uint8_t *udp;
    uint8_t *dhcp;
    uint16_t ip_header_len;
    uint16_t udp_len;
    uint16_t dhcp_len;
    uint16_t pos;
    uint8_t msg_type = 0U;

    if(ethernet_length < 282U)
        return 0;
    if(probe_get_u16_be(&g_ap6212_net_rx[12]) != 0x0800U)
        return 0;
    if(ip[9] != 17U)
        return 0;

    ip_header_len = (uint16_t)((ip[0] & 0x0FU) * 4U);
    if(ip_header_len < 20U || ethernet_length < 14U + ip_header_len + 8U)
        return 0;

    udp = &g_ap6212_net_rx[14U + ip_header_len];
    if(probe_get_u16_be(&udp[2]) != 68U)
        return 0;
    udp_len = probe_get_u16_be(&udp[4]);
    if(udp_len < 8U)
        return 0;

    dhcp = &udp[8];
    dhcp_len = (uint16_t)(udp_len - 8U);
    if(dhcp_len < 241U)
        return 0;
    if(dhcp[0] != 2U || probe_get_u32_be(&dhcp[4]) != xid ||
       probe_get_u32_be(&dhcp[236]) != 0x63825363U)
    {
        return 0;
    }

    g_ap6212_ip_addr = probe_get_u32_be(&dhcp[16]);
    pos = 240U;
    while(pos < dhcp_len)
    {
        uint8_t code = dhcp[pos++];
        uint8_t len;

        if(code == 0U)
            continue;
        if(code == 255U)
            break;
        if(pos >= dhcp_len)
            break;
        len = dhcp[pos++];
        if(pos + len > dhcp_len)
            break;

        if(code == 53U && len == 1U)
            msg_type = dhcp[pos];
        else if(code == 1U && len >= 4U)
            g_ap6212_net_mask = probe_get_u32_be(&dhcp[pos]);
        else if(code == 3U && len >= 4U)
            g_ap6212_gateway = probe_get_u32_be(&dhcp[pos]);
        else if(code == 6U && len >= 4U)
            g_ap6212_dns = probe_get_u32_be(&dhcp[pos]);
        else if(code == 54U && len >= 4U)
            g_ap6212_dhcp_server = probe_get_u32_be(&dhcp[pos]);

        pos = (uint16_t)(pos + len);
    }

    if(msg_type != expected_type)
        return 0;

    probe_printf("[dhcp] msg=%u yiaddr=%lu.%lu.%lu.%lu server=%lu.%lu.%lu.%lu\r\n",
                 (unsigned int)msg_type,
                 (unsigned long)((g_ap6212_ip_addr >> 24) & 0xFFU),
                 (unsigned long)((g_ap6212_ip_addr >> 16) & 0xFFU),
                 (unsigned long)((g_ap6212_ip_addr >> 8) & 0xFFU),
                 (unsigned long)(g_ap6212_ip_addr & 0xFFU),
                 (unsigned long)((g_ap6212_dhcp_server >> 24) & 0xFFU),
                 (unsigned long)((g_ap6212_dhcp_server >> 16) & 0xFFU),
                 (unsigned long)((g_ap6212_dhcp_server >> 8) & 0xFFU),
                 (unsigned long)(g_ap6212_dhcp_server & 0xFFU));
    return 1;
}

static int app_ap6212_raw_dhcp(uint32_t sdio_base)
{
    const uint32_t xid = 0xA6212D01U;
    uint16_t tx_len;
    uint16_t rx_len = 0U;
    int status;

    g_ap6212_ip_addr = 0U;
    g_ap6212_net_mask = 0U;
    g_ap6212_gateway = 0U;
    g_ap6212_dns = 0U;
    g_ap6212_dhcp_server = 0U;

    for(uint32_t attempt = 0U; attempt < 3U; attempt++)
    {
        tx_len = app_ap6212_build_dhcp_frame(1U, xid, 0U, 0U);
        probe_printf("[dhcp] discover attempt=%lu len=%u\r\n",
                     (unsigned long)(attempt + 1U),
                     (unsigned int)tx_len);
        status = app_ap6212_sdpcm_write_data(g_ap6212_net_tx, tx_len);
        if(status != 0)
            return -1;

        for(uint32_t wait = 0U; wait < 150U; wait++)
        {
            status = app_ap6212_read_ethernet_frame(sdio_base, &rx_len, 20U);
            if(status == 0 && app_ap6212_parse_dhcp_frame(rx_len, xid, 2U) > 0)
                goto got_offer;
        }
    }
    return -2;

got_offer:
    if(g_ap6212_ip_addr == 0U || g_ap6212_dhcp_server == 0U)
        return -3;

    for(uint32_t attempt = 0U; attempt < 3U; attempt++)
    {
        tx_len = app_ap6212_build_dhcp_frame(3U,
                                             xid,
                                             g_ap6212_ip_addr,
                                             g_ap6212_dhcp_server);
        probe_printf("[dhcp] request attempt=%lu len=%u\r\n",
                     (unsigned long)(attempt + 1U),
                     (unsigned int)tx_len);
        status = app_ap6212_sdpcm_write_data(g_ap6212_net_tx, tx_len);
        if(status != 0)
            return -4;

        for(uint32_t wait = 0U; wait < 150U; wait++)
        {
            status = app_ap6212_read_ethernet_frame(sdio_base, &rx_len, 20U);
            if(status == 0 && app_ap6212_parse_dhcp_frame(rx_len, xid, 5U) > 0)
                return 0;
        }
    }

    return -5;
}

static uint16_t app_ap6212_build_arp_request(void)
{
    static const uint8_t broadcast_mac[6] =
        {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

    memset(g_ap6212_net_tx, 0, sizeof(g_ap6212_net_tx));
    memcpy(&g_ap6212_net_tx[0], broadcast_mac, 6U);
    memcpy(&g_ap6212_net_tx[6], g_ap6212_wifi_mac, 6U);
    probe_put_u16_be(&g_ap6212_net_tx[12], 0x0806U);
    probe_put_u16_be(&g_ap6212_net_tx[14], 1U);
    probe_put_u16_be(&g_ap6212_net_tx[16], 0x0800U);
    g_ap6212_net_tx[18] = 6U;
    g_ap6212_net_tx[19] = 4U;
    probe_put_u16_be(&g_ap6212_net_tx[20], 1U);
    memcpy(&g_ap6212_net_tx[22], g_ap6212_wifi_mac, 6U);
    probe_put_u32_be(&g_ap6212_net_tx[28], g_ap6212_ip_addr);
    probe_put_u32_be(&g_ap6212_net_tx[38], g_ap6212_gateway);
    return 42U;
}

static int app_ap6212_parse_arp_reply(uint16_t ethernet_length)
{
    if(ethernet_length < 42U)
        return 0;
    if(probe_get_u16_be(&g_ap6212_net_rx[12]) != 0x0806U)
        return 0;
    if(probe_get_u16_be(&g_ap6212_net_rx[20]) != 2U)
        return 0;
    if(probe_get_u32_be(&g_ap6212_net_rx[28]) != g_ap6212_gateway ||
       probe_get_u32_be(&g_ap6212_net_rx[38]) != g_ap6212_ip_addr)
    {
        return 0;
    }

    memcpy(g_ap6212_gateway_mac, &g_ap6212_net_rx[22], 6U);
    probe_printf("[arp] gateway mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 (unsigned int)g_ap6212_gateway_mac[0],
                 (unsigned int)g_ap6212_gateway_mac[1],
                 (unsigned int)g_ap6212_gateway_mac[2],
                 (unsigned int)g_ap6212_gateway_mac[3],
                 (unsigned int)g_ap6212_gateway_mac[4],
                 (unsigned int)g_ap6212_gateway_mac[5]);
    return 1;
}

static int app_ap6212_raw_arp(uint32_t sdio_base)
{
    uint16_t tx_len;
    uint16_t rx_len = 0U;
    int status;

    for(uint32_t attempt = 0U; attempt < 3U; attempt++)
    {
        tx_len = app_ap6212_build_arp_request();
        probe_printf("[arp] request attempt=%lu\r\n", (unsigned long)(attempt + 1U));
        status = app_ap6212_sdpcm_write_data(g_ap6212_net_tx, tx_len);
        if(status != 0)
            return -1;

        for(uint32_t wait = 0U; wait < 100U; wait++)
        {
            status = app_ap6212_read_ethernet_frame(sdio_base, &rx_len, 20U);
            if(status == 0 && app_ap6212_parse_arp_reply(rx_len) > 0)
                return 0;
        }
    }

    return -2;
}

static uint16_t app_ap6212_build_icmp_echo(void)
{
    static const uint8_t payload[] = "ap6212-ping";
    uint8_t *ip = &g_ap6212_net_tx[14];
    uint8_t *icmp = &g_ap6212_net_tx[34];
    uint16_t icmp_len = (uint16_t)(8U + sizeof(payload) - 1U);
    uint16_t ip_total = (uint16_t)(20U + icmp_len);

    memset(g_ap6212_net_tx, 0, sizeof(g_ap6212_net_tx));
    memcpy(&g_ap6212_net_tx[0], g_ap6212_gateway_mac, 6U);
    memcpy(&g_ap6212_net_tx[6], g_ap6212_wifi_mac, 6U);
    probe_put_u16_be(&g_ap6212_net_tx[12], 0x0800U);

    ip[0] = 0x45U;
    probe_put_u16_be(&ip[2], ip_total);
    probe_put_u16_be(&ip[4], 0x6213U);
    ip[8] = 64U;
    ip[9] = 1U;
    probe_put_u32_be(&ip[12], g_ap6212_ip_addr);
    probe_put_u32_be(&ip[16], g_ap6212_gateway);
    probe_put_u16_be(&ip[10], app_ap6212_ip_checksum(ip, 20U));

    icmp[0] = 8U;
    icmp[1] = 0U;
    probe_put_u16_be(&icmp[4], 0x6212U);
    probe_put_u16_be(&icmp[6], 1U);
    memcpy(&icmp[8], payload, sizeof(payload) - 1U);
    probe_put_u16_be(&icmp[2], app_ap6212_ip_checksum(icmp, icmp_len));

    return (uint16_t)(14U + ip_total);
}

static int app_ap6212_parse_icmp_reply(uint16_t ethernet_length)
{
    uint8_t *ip = &g_ap6212_net_rx[14];
    uint8_t *icmp;
    uint16_t ip_header_len;

    if(ethernet_length < 42U)
        return 0;
    if(probe_get_u16_be(&g_ap6212_net_rx[12]) != 0x0800U)
        return 0;
    if(ip[9] != 1U ||
       probe_get_u32_be(&ip[12]) != g_ap6212_gateway ||
       probe_get_u32_be(&ip[16]) != g_ap6212_ip_addr)
    {
        return 0;
    }

    ip_header_len = (uint16_t)((ip[0] & 0x0FU) * 4U);
    if(ethernet_length < 14U + ip_header_len + 8U)
        return 0;
    icmp = &g_ap6212_net_rx[14U + ip_header_len];
    if(icmp[0] == 0U &&
       probe_get_u16_be(&icmp[4]) == 0x6212U &&
       probe_get_u16_be(&icmp[6]) == 1U)
    {
        return 1;
    }

    return 0;
}

static int app_ap6212_raw_ping_gateway(uint32_t sdio_base)
{
    uint16_t tx_len;
    uint16_t rx_len = 0U;
    int status;

    tx_len = app_ap6212_build_icmp_echo();
    probe_printf("[ping] gateway echo len=%u\r\n", (unsigned int)tx_len);
    status = app_ap6212_sdpcm_write_data(g_ap6212_net_tx, tx_len);
    if(status != 0)
        return -1;

    for(uint32_t wait = 0U; wait < 150U; wait++)
    {
        status = app_ap6212_read_ethernet_frame(sdio_base, &rx_len, 20U);
        if(status == 0 && app_ap6212_parse_icmp_reply(rx_len) > 0)
        {
            probe_write("[ping] gateway OK\r\n");
            return 0;
        }
    }

    return -2;
}

static int app_ap6212_raw_network_smoke(uint32_t sdio_base)
{
    int status;

    status = app_ap6212_raw_dhcp(sdio_base);
    probe_printf("[dhcp] status=%d\r\n", status);
    if(status != 0)
        return -1;

    app_ap6212_print_ip("[net] ip=", g_ap6212_ip_addr);
    app_ap6212_print_ip("[net] mask=", g_ap6212_net_mask);
    app_ap6212_print_ip("[net] gateway=", g_ap6212_gateway);
    app_ap6212_print_ip("[net] dns=", g_ap6212_dns);

    status = app_ap6212_raw_arp(sdio_base);
    probe_printf("[arp] status=%d\r\n", status);
    if(status != 0)
        return -2;

    status = app_ap6212_raw_ping_gateway(sdio_base);
    probe_printf("[ping] status=%d\r\n", status);
    return (status == 0) ? 0 : -3;
}

static int app_ap6212_wifi_join_smoke(uint32_t sdio_base)
{
    int status;

    probe_printf("[wifi] join start ssid=%s\r\n", APP_AP6212_WIFI_SSID);

    (void)app_ap6212_bcdc_enable_events(sdio_base);

    status = app_ap6212_bcdc_set_command_u32(sdio_base,
                                             APP_AP6212_BRCMF_C_UP,
                                             1U);
    if(status != 0)
        return -1;

    status = app_ap6212_bcdc_set_command_u32(sdio_base,
                                             APP_AP6212_BRCMF_C_SET_INFRA,
                                             1U);
    if(status != 0)
        probe_printf("[wifi-cfg] SET_INFRA optional failed status=%d, continue\r\n",
                     status);

    status = app_ap6212_bcdc_set_iovar_u32(sdio_base, "mpc", 0U);
    if(status != 0)
        return -3;

    status = app_ap6212_bcdc_set_iovar_u32(sdio_base, "auth", 0U);
    if(status != 0)
        return -4;

    status = app_ap6212_bcdc_set_iovar_u32(sdio_base,
                                           "wpa_auth",
                                           APP_AP6212_WPA2_AUTH_PSK);
    if(status != 0)
        return -5;

    status = app_ap6212_bcdc_set_iovar_u32(sdio_base,
                                           "wsec",
                                           APP_AP6212_AES_ENABLED);
    if(status != 0)
        return -6;

    status = app_ap6212_bcdc_set_iovar_u32(sdio_base, "sup_wpa", 1U);
    if(status != 0)
        return -7;

    status = app_ap6212_bcdc_set_passphrase(sdio_base,
                                            APP_AP6212_WIFI_PASSWORD);
    if(status != 0)
        return -8;

    status = app_ap6212_bcdc_set_ssid(sdio_base, APP_AP6212_WIFI_SSID);
    if(status != 0)
        return -9;

    status = app_ap6212_wifi_wait_link(sdio_base);
    if(status != 0)
    {
        (void)app_ap6212_wifi_query_assoc(sdio_base);
        return -10;
    }

    status = app_ap6212_wifi_query_assoc(sdio_base);
    probe_printf("[wifi] join verify status=%d\r\n", status);
    if(status != 0)
        return status;

#if APP_AP6212_ENABLE_RAW_NET_SMOKE
    g_ap6212_wifi_ready = 1U;
    status = app_ap6212_raw_network_smoke(sdio_base);
    probe_printf("[net-raw] smoke status=%d\r\n", status);
    return status;
#else
    probe_write("[net-raw] smoke skipped\r\n");
    g_ap6212_wifi_ready = 1U;
    return 0;
#endif
}

static int app_ap6212_bcdc_smoke(uint32_t sdio_base)
{
    static const uint8_t cur_etheraddr_name[] = "cur_etheraddr";
    static const uint8_t ver_name[] = "ver";
    uint16_t response_length = 0U;
    uint32_t version;
    int status;

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_GET_VERSION,
                                  0U,
                                  NULL,
                                  0U,
                                  4U,
                                  g_ap6212_bcdc_data,
                                  4U,
                                  &response_length);
    if(status != 0 || response_length < 4U)
    {
        probe_printf("[bcdc] GET_VERSION failed status=%d len=%u\r\n",
                     status,
                     (unsigned int)response_length);
        return -1;
    }

    version = probe_get_u32_le(g_ap6212_bcdc_data);
    probe_printf("[bcdc] GET_VERSION value=%lu raw=%02X %02X %02X %02X\r\n",
                 (unsigned long)version,
                 (unsigned int)g_ap6212_bcdc_data[0],
                 (unsigned int)g_ap6212_bcdc_data[1],
                 (unsigned int)g_ap6212_bcdc_data[2],
                 (unsigned int)g_ap6212_bcdc_data[3]);

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_GET_VAR,
                                  0U,
                                  cur_etheraddr_name,
                                  (uint16_t)sizeof(cur_etheraddr_name),
                                  (uint16_t)(sizeof(cur_etheraddr_name) + 6U),
                                  g_ap6212_bcdc_data,
                                  6U,
                                  &response_length);
    if(status != 0 || response_length < 6U)
    {
        probe_printf("[bcdc] cur_etheraddr failed status=%d len=%u\r\n",
                     status,
                     (unsigned int)response_length);
        return -2;
    }

    probe_printf("[wifi] mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                 (unsigned int)g_ap6212_bcdc_data[0],
                 (unsigned int)g_ap6212_bcdc_data[1],
                 (unsigned int)g_ap6212_bcdc_data[2],
                 (unsigned int)g_ap6212_bcdc_data[3],
                 (unsigned int)g_ap6212_bcdc_data[4],
                 (unsigned int)g_ap6212_bcdc_data[5]);
    memcpy(g_ap6212_wifi_mac, g_ap6212_bcdc_data, sizeof(g_ap6212_wifi_mac));

    status = app_ap6212_bcdc_dcmd(sdio_base,
                                  APP_AP6212_BRCMF_C_GET_VAR,
                                  0U,
                                  ver_name,
                                  (uint16_t)sizeof(ver_name),
                                  (uint16_t)(sizeof(ver_name) + 128U),
                                  g_ap6212_bcdc_data,
                                  (uint16_t)(sizeof(g_ap6212_bcdc_data) - 1U),
                                  &response_length);
    if(status == 0 && response_length != 0U)
    {
        if(response_length >= sizeof(g_ap6212_bcdc_data))
            response_length = (uint16_t)(sizeof(g_ap6212_bcdc_data) - 1U);
        g_ap6212_bcdc_data[response_length] = '\0';
        app_ap6212_sanitize_ascii(g_ap6212_bcdc_data, response_length);
        probe_printf("[wifi] fwver=%s\r\n", (const char *)g_ap6212_bcdc_data);
    }
    else
    {
        probe_printf("[bcdc] ver optional query status=%d len=%u\r\n",
                     status,
                     (unsigned int)response_length);
    }

    return 0;
}

static int app_ap6212_sdpcm_ready_smoke(const bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t sdio_base = 0U;
    uint32_t intstatus = 0U;
    uint32_t hmb_data = 0U;
    uint32_t host_version =
        APP_AP6212_SDPCM_PROT_VERSION << APP_AP6212_HMB_DATA_VERSION_SHIFT;
    int status;

    status = app_ap6212_find_core_base(probe, APP_AP6212_CORE_SDIO_DEV, &sdio_base);
    if(status != 0)
        return -1;

    g_ap6212_sdio_base = sdio_base;
    g_ap6212_wifi_ready = 0U;

    status = app_ap6212_backplane_write32(sdio_base +
                                          APP_AP6212_SDPCM_TOSBMAILBOXDATA,
                                          host_version);
    probe_printf("[sdpcm] host version write status=%d base=0x%08lX value=0x%08lX\r\n",
                 status,
                 (unsigned long)sdio_base,
                 (unsigned long)host_version);
    if(status != 0)
        return -2;

    status = app_ap6212_backplane_write32(sdio_base +
                                          APP_AP6212_SDPCM_HOSTINTMASK,
                                          APP_AP6212_HOSTINTMASK);
    probe_printf("[sdpcm] hostintmask write status=%d mask=0x%08lX\r\n",
                 status,
                 (unsigned long)APP_AP6212_HOSTINTMASK);
    if(status != 0)
        return -3;

    for(uint32_t retry = 0U; retry < 1000U; retry++)
    {
        status = app_ap6212_backplane_read32(sdio_base +
                                             APP_AP6212_SDPCM_INTSTATUS,
                                             &intstatus);
        if(status != 0)
            return -4;
        status = app_ap6212_backplane_read32(sdio_base +
                                             APP_AP6212_SDPCM_TOHOSTMAILBOXDATA,
                                             &hmb_data);
        if(status != 0)
            return -5;

        if((hmb_data & (APP_AP6212_HMB_DATA_DEVREADY |
                        APP_AP6212_HMB_DATA_FWREADY)) != 0U)
        {
            uint32_t version =
                (hmb_data & APP_AP6212_HMB_DATA_VERSION_MASK) >>
                APP_AP6212_HMB_DATA_VERSION_SHIFT;

            probe_printf("[sdpcm] ready retry=%lu intstatus=0x%08lX hmb=0x%08lX version=%lu\r\n",
                         (unsigned long)retry,
                         (unsigned long)intstatus,
                         (unsigned long)hmb_data,
                         (unsigned long)version);
            (void)app_ap6212_backplane_write32(sdio_base +
                                               APP_AP6212_SDPCM_TOSBMAILBOX,
                                               APP_AP6212_SMB_INT_ACK);
            (void)app_ap6212_read_sdpcm_shared(probe);
            (void)app_ap6212_f2_firstread_smoke();
            if(version != APP_AP6212_SDPCM_PROT_VERSION)
                return -6;

            (void)app_ap6212_sdpcm_clear_pending(sdio_base);
            status = app_ap6212_bcdc_smoke(sdio_base);
            probe_printf("[bcdc] smoke status=%d\r\n", status);
            if(status != 0)
                return -8;

            status = app_ap6212_wifi_join_smoke(sdio_base);
            probe_printf("[wifi] join smoke status=%d\r\n", status);
            return (status == 0) ? 0 : -9;
        }

        tx_thread_sleep(1U);
    }

    probe_printf("[sdpcm] ready timeout intstatus=0x%08lX hmb=0x%08lX\r\n",
                 (unsigned long)intstatus,
                 (unsigned long)hmb_data);
    (void)app_ap6212_read_sdpcm_shared(probe);
    return -7;
}

static int app_ap6212_verify_ram_from_qspi(uint32_t qspi_address,
                                           uint32_t ram_address,
                                           uint32_t length)
{
    if(length > sizeof(g_ap6212_fw_chunk))
        return -1;
    if(bsp_qspi_flash_read(qspi_address, g_ap6212_fw_chunk, length) != 0)
        return -2;
    if(bsp_ap6212_sdio_ram_read(ram_address, g_ap6212_fw_verify, length) != 0)
        return -3;
    return (memcmp(g_ap6212_fw_chunk, g_ap6212_fw_verify, length) == 0) ? 0 : -4;
}

static int app_ap6212_prepare_nvram(const app_ap6212_fw_bundle_info_t *info,
                                    uint32_t *prepared_length)
{
    uint32_t read_pos = 0U;
    uint32_t write_pos = 0U;
    uint32_t token;

    if(info == NULL || prepared_length == NULL)
        return -1;
    if(info->nvram_length == 0U || info->nvram_length >= sizeof(g_ap6212_nvram_buffer))
        return -2;
    if(bsp_qspi_flash_read(APP_QSPI_LOADER_BASE + info->nvram_offset,
                           g_ap6212_nvram_buffer,
                           info->nvram_length) != 0)
        return -3;

    while(read_pos < info->nvram_length)
    {
        uint32_t line_start;
        uint32_t line_end;

        while(read_pos < info->nvram_length &&
              (g_ap6212_nvram_buffer[read_pos] == '\r' ||
               g_ap6212_nvram_buffer[read_pos] == '\n'))
        {
            read_pos++;
        }

        line_start = read_pos;
        while(read_pos < info->nvram_length &&
              g_ap6212_nvram_buffer[read_pos] != '\r' &&
              g_ap6212_nvram_buffer[read_pos] != '\n' &&
              g_ap6212_nvram_buffer[read_pos] != '\0')
        {
            read_pos++;
        }
        line_end = read_pos;

        while(line_start < line_end &&
              (g_ap6212_nvram_buffer[line_start] == ' ' ||
               g_ap6212_nvram_buffer[line_start] == '\t'))
        {
            line_start++;
        }
        while(line_end > line_start &&
              (g_ap6212_nvram_buffer[line_end - 1U] == ' ' ||
               g_ap6212_nvram_buffer[line_end - 1U] == '\t'))
        {
            line_end--;
        }

        if(line_start == line_end || g_ap6212_nvram_buffer[line_start] == '#')
            continue;

        if(write_pos + (line_end - line_start) + 1U + 8U > sizeof(g_ap6212_nvram_buffer))
            return -4;

        memmove(&g_ap6212_nvram_buffer[write_pos],
                &g_ap6212_nvram_buffer[line_start],
                line_end - line_start);
        write_pos += line_end - line_start;
        g_ap6212_nvram_buffer[write_pos++] = '\0';
    }

    if(write_pos == 0U || write_pos + 8U > sizeof(g_ap6212_nvram_buffer))
        return -5;

    g_ap6212_nvram_buffer[write_pos++] = '\0';
    while((write_pos & 0x3U) != 0U)
        g_ap6212_nvram_buffer[write_pos++] = '\0';

    token = write_pos / 4U;
    token = (~token << 16) | (token & 0x0000FFFFU);
    g_ap6212_nvram_buffer[write_pos++] = (uint8_t)token;
    g_ap6212_nvram_buffer[write_pos++] = (uint8_t)(token >> 8);
    g_ap6212_nvram_buffer[write_pos++] = (uint8_t)(token >> 16);
    g_ap6212_nvram_buffer[write_pos++] = (uint8_t)(token >> 24);

    *prepared_length = write_pos;
    return 0;
}

static int app_ap6212_download_firmware(const app_ap6212_fw_bundle_info_t *info,
                                        const bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t offset = 0U;
    uint32_t rstvec = 0U;
    uint32_t nvram_length = 0U;
    uint32_t nvram_address = 0U;
    uint8_t io_enable = 0U;
    uint8_t io_ready = 0U;
    int status;

    if(info == NULL || probe == NULL)
        return -1;
    if(!app_ap6212_fw_bundle_crc_ok(info))
        return -2;
    if(info->firmware_length == 0U ||
       info->firmware_length > probe->socram_ram_size ||
       probe->socram_status != 0 ||
       probe->ram_rw_status != 0)
    {
        return -3;
    }

    if(bsp_qspi_flash_read(APP_QSPI_LOADER_BASE + info->firmware_offset,
                           g_ap6212_fw_chunk,
                           4U) != 0)
        return -4;
    rstvec = probe_get_u32_le(g_ap6212_fw_chunk);

    probe_printf("[fw] download start rstvec=0x%08lX fw=%lu ram_base=0x%08lX\r\n",
                 (unsigned long)rstvec,
                 (unsigned long)info->firmware_length,
                 (unsigned long)probe->socram_ram_base);

    while(offset < info->firmware_length)
    {
        uint32_t chunk = info->firmware_length - offset;
        if(chunk > sizeof(g_ap6212_fw_chunk))
            chunk = sizeof(g_ap6212_fw_chunk);

        if(bsp_qspi_flash_read(APP_QSPI_LOADER_BASE + info->firmware_offset + offset,
                               g_ap6212_fw_chunk,
                               chunk) != 0)
            return -5;
        status = bsp_ap6212_sdio_ram_write(probe->socram_ram_base + offset,
                                           g_ap6212_fw_chunk,
                                           chunk);
        if(status != 0)
        {
            bsp_ap6212_sdio_ext_debug_t debug;
            bsp_ap6212_sdio_get_last_ext_debug(&debug);
            probe_printf("[fw] write failed offset=%lu status=%ld sta=0x%08lX resp=0x%08lX xfer=%lu/%lu addr=0x%05lX wr=%lu fn=%lu inc=%lu\r\n",
                         (unsigned long)offset,
                         (long)status,
                         (unsigned long)debug.sta,
                         (unsigned long)debug.resp1,
                         (unsigned long)debug.transferred,
                         (unsigned long)debug.length,
                         (unsigned long)debug.address,
                         (unsigned long)debug.write,
                         (unsigned long)debug.function,
                         (unsigned long)debug.increment);
            return -6;
        }

        offset += chunk;
        if((offset & 0x0000FFFFU) == 0U || offset == info->firmware_length)
        {
            probe_printf("[fw] wrote %lu/%lu\r\n",
                         (unsigned long)offset,
                         (unsigned long)info->firmware_length);
        }
    }

    status = app_ap6212_verify_ram_from_qspi(APP_QSPI_LOADER_BASE + info->firmware_offset,
                                            probe->socram_ram_base,
                                            sizeof(g_ap6212_fw_chunk));
    if(status != 0)
        return -7;
    status = app_ap6212_verify_ram_from_qspi(APP_QSPI_LOADER_BASE + info->firmware_offset +
                                            info->firmware_length - sizeof(g_ap6212_fw_chunk),
                                            probe->socram_ram_base +
                                            info->firmware_length - sizeof(g_ap6212_fw_chunk),
                                            sizeof(g_ap6212_fw_chunk));
    if(status != 0)
        return -8;

    status = app_ap6212_prepare_nvram(info, &nvram_length);
    if(status != 0)
        return -9;
    nvram_address = probe->socram_ram_base + probe->socram_ram_size - nvram_length;
    probe_printf("[nvram] prepared %lu bytes target=0x%08lX\r\n",
                 (unsigned long)nvram_length,
                 (unsigned long)nvram_address);
    status = bsp_ap6212_sdio_ram_write(nvram_address,
                                       g_ap6212_nvram_buffer,
                                       nvram_length);
    if(status != 0)
    {
        bsp_ap6212_sdio_ext_debug_t debug;
        bsp_ap6212_sdio_get_last_ext_debug(&debug);
        probe_printf("[nvram] write failed status=%ld sta=0x%08lX resp=0x%08lX xfer=%lu/%lu addr=0x%05lX wr=%lu fn=%lu inc=%lu\r\n",
                     (long)status,
                     (unsigned long)debug.sta,
                     (unsigned long)debug.resp1,
                     (unsigned long)debug.transferred,
                     (unsigned long)debug.length,
                     (unsigned long)debug.address,
                     (unsigned long)debug.write,
                     (unsigned long)debug.function,
                     (unsigned long)debug.increment);
        return -10;
    }
    status = bsp_ap6212_sdio_ram_read(nvram_address,
                                      g_ap6212_nvram_verify,
                                      nvram_length);
    if(status != 0)
    {
        bsp_ap6212_sdio_ext_debug_t debug;
        bsp_ap6212_sdio_get_last_ext_debug(&debug);
        probe_printf("[nvram] read failed status=%ld sta=0x%08lX resp=0x%08lX xfer=%lu/%lu addr=0x%05lX wr=%lu fn=%lu inc=%lu\r\n",
                     (long)status,
                     (unsigned long)debug.sta,
                     (unsigned long)debug.resp1,
                     (unsigned long)debug.transferred,
                     (unsigned long)debug.length,
                     (unsigned long)debug.address,
                     (unsigned long)debug.write,
                     (unsigned long)debug.function,
                     (unsigned long)debug.increment);
        return -11;
    }
    if(memcmp(g_ap6212_nvram_buffer, g_ap6212_nvram_verify, nvram_length) != 0)
    {
        probe_printf("[nvram] verify mismatch length=%lu address=0x%08lX\r\n",
                     (unsigned long)nvram_length,
                     (unsigned long)nvram_address);
        return -12;
    }

    probe_printf("[nvram] wrote %lu bytes at 0x%08lX\r\n",
                 (unsigned long)nvram_length,
                 (unsigned long)nvram_address);

    status = bsp_ap6212_sdio_release_cm3(probe);
    probe_printf("[fw] release cm3 status=%d\r\n", status);
    if(status != 0)
        return -13;

    status = bsp_ap6212_sdio_enable_f2(&io_enable, &io_ready);
    probe_printf("[ap6212-sdio] F2 enable status=%d en=0x%02X ready=0x%02X\r\n",
                 status,
                 (unsigned int)io_enable,
                 (unsigned int)io_ready);
    if(status != 0)
        return -14;

    status = app_ap6212_sdpcm_ready_smoke(probe);
    probe_printf("[sdpcm] ready smoke status=%d\r\n", status);
    if(status != 0)
        return -15;

    return 0;
}

static void app_ap6212_sdio_probe_entry(ULONG thread_input)
{
    bsp_ap6212_sdio_probe_t probe;
    bsp_qspi_flash_id_t flash_id;
    app_ap6212_fw_bundle_info_t fw_info;
    bsp_ap6212_sdio_status_t status;
    int flash_status;
    int fw_status;
    int download_status = -999;

    (void)thread_input;

    tx_thread_sleep(APP_AP6212_SDIO_PROBE_DELAY_MS);
    flash_status = bsp_qspi_flash_read_id(&flash_id);
    app_qspi_flash_probe_id = flash_id;
    app_qspi_flash_probe_status = flash_status;
    if(flash_status == 0)
    {
        probe_printf("\r\n[qspi] JEDEC %02X %02X %02X mode=%u capacity=%lu bytes\r\n",
                     (unsigned int)flash_id.manufacturer_id,
                     (unsigned int)flash_id.memory_type,
                     (unsigned int)flash_id.capacity_id,
                     (unsigned int)flash_id.read_mode,
                     (unsigned long)flash_id.capacity_bytes);
    }
    else
    {
        probe_printf("\r\n[qspi] JEDEC read failed status=%d\r\n", flash_status);
    }

    fw_status = app_ap6212_fw_bundle_read_info(&fw_info);
    if(fw_status == 0)
    {
        probe_printf("[ap6212-fw] bundle version=%lu total=%lu header=%lu crc=%s\r\n",
                     (unsigned long)fw_info.version,
                     (unsigned long)fw_info.total_size,
                     (unsigned long)fw_info.header_size,
                     app_ap6212_fw_bundle_crc_ok(&fw_info) ? "OK" : "BAD");
        probe_printf("[ap6212-fw] fw off=%lu len=%lu crc=0x%08lX read=0x%08lX\r\n",
                     (unsigned long)fw_info.firmware_offset,
                     (unsigned long)fw_info.firmware_length,
                     (unsigned long)fw_info.firmware_crc32,
                     (unsigned long)fw_info.firmware_crc32_readback);
        probe_printf("[ap6212-fw] nvram off=%lu len=%lu crc=0x%08lX read=0x%08lX\r\n",
                     (unsigned long)fw_info.nvram_offset,
                     (unsigned long)fw_info.nvram_length,
                     (unsigned long)fw_info.nvram_crc32,
                     (unsigned long)fw_info.nvram_crc32_readback);
    }
    else
    {
        probe_printf("[ap6212-fw] bundle read failed status=%d\r\n", fw_status);
    }

    probe_write("\r\n[ap6212-sdio] probe start\r\n");

    status = bsp_ap6212_sdio_probe(&probe);
    app_ap6212_sdio_probe_result = probe;
    app_ap6212_sdio_probe_status = (int32_t)status;
    app_ap6212_sdio_probe_done = 1U;

    if(status != BSP_AP6212_SDIO_OK)
    {
        probe_printf("[ap6212-sdio] probe failed status=%d sta=0x%08lX cmd5_0=0x%08lX cmd5=0x%08lX\r\n",
                     (int)status,
                     (unsigned long)probe.last_sta,
                     (unsigned long)probe.cmd5_initial,
                     (unsigned long)probe.cmd5_ready);
        return;
    }

    probe_printf("[ap6212-sdio] CMD5 initial=0x%08lX ready=0x%08lX\r\n",
                 (unsigned long)probe.cmd5_initial,
                 (unsigned long)probe.cmd5_ready);
    probe_printf("[ap6212-sdio] OCR=0x%06lX funcs=%u mem=%u RCA=0x%04X\r\n",
                 (unsigned long)probe.ocr,
                 (unsigned int)probe.io_functions,
                 (unsigned int)probe.memory_present,
                 (unsigned int)probe.rca);
    probe_printf("[ap6212-sdio] CCCR=%02X %02X %02X %02X %02X %02X %02X %02X cap=0x%02X\r\n",
                 (unsigned int)probe.cccr[0],
                 (unsigned int)probe.cccr[1],
                 (unsigned int)probe.cccr[2],
                 (unsigned int)probe.cccr[3],
                 (unsigned int)probe.cccr[4],
                 (unsigned int)probe.cccr[5],
                 (unsigned int)probe.cccr[6],
                 (unsigned int)probe.cccr[7],
                 (unsigned int)probe.cccr[8]);
    probe_printf("[ap6212-sdio] CIS common=0x%05lX manf=0x%04X card=0x%04X\r\n",
                 (unsigned long)probe.common_cis_ptr,
                 (unsigned int)probe.common_manf,
                 (unsigned int)probe.common_card);
    probe_printf("[ap6212-sdio] F1 code=0x%02X cis=0x%05lX manf=0x%04X card=0x%04X\r\n",
                 (unsigned int)probe.func_code[0],
                 (unsigned long)probe.func_cis_ptr[0],
                 (unsigned int)probe.func_manf[0],
                 (unsigned int)probe.func_card[0]);
    probe_printf("[ap6212-sdio] F2 code=0x%02X cis=0x%05lX manf=0x%04X card=0x%04X\r\n",
                 (unsigned int)probe.func_code[1],
                 (unsigned long)probe.func_cis_ptr[1],
                 (unsigned int)probe.func_manf[1],
                 (unsigned int)probe.func_card[1]);
    probe_printf("[ap6212-sdio] IO before en=0x%02X ready=0x%02X after en=0x%02X ready=0x%02X\r\n",
                 (unsigned int)probe.io_enable_before,
                 (unsigned int)probe.io_ready_before,
                 (unsigned int)probe.io_enable_after,
                 (unsigned int)probe.io_ready_after);
    probe_printf("[ap6212-sdio] F1 sleepcsr %02X->%02X chipclk %02X->%02X\r\n",
                 (unsigned int)probe.sleepcsr_before,
                 (unsigned int)probe.sleepcsr_after,
                 (unsigned int)probe.chipclk_before,
                 (unsigned int)probe.chipclk_after);
    probe_printf("[ap6212-sdio] F1 buscore prep status=%ld alp_req=0x%02X force_alp=0x%02X pullup=0x%02X host_wake=%u\r\n",
                 (long)probe.buscore_prepare_status,
                 (unsigned int)probe.chipclk_alp_req,
                 (unsigned int)probe.chipclk_force_alp,
                 (unsigned int)probe.sdio_pullup_after,
                 (unsigned int)probe.wifi_host_wake);
    probe_printf("[ap6212-sdio] CMD53 backplane chipid status=%ld sta=0x%08lX resp=0x%08lX chipid=0x%08lX cmd52_chipclk=0x%02X bytes=%02X %02X %02X %02X\r\n",
                 (long)probe.cmd53_smoke_status,
                 (unsigned long)probe.cmd53_last_sta,
                 (unsigned long)probe.cmd53_resp1,
                 (unsigned long)probe.cmd53_backplane_chipid,
                 (unsigned int)probe.cmd53_cmd52_chipclk,
                 (unsigned int)probe.cmd53_bytes[0],
                 (unsigned int)probe.cmd53_bytes[1],
                 (unsigned int)probe.cmd53_bytes[2],
                 (unsigned int)probe.cmd53_bytes[3]);
    probe_printf("[brcm] core scan status=%ld erom=0x%08lX count=%u\r\n",
                 (long)probe.core_scan_status,
                 (unsigned long)probe.erom_ptr,
                 (unsigned int)probe.core_count);
    for(uint8_t i = 0U; i < probe.core_count && i < BSP_AP6212_SDIO_CORE_SCAN_MAX; i++)
    {
        probe_printf("[brcm] core[%u] id=0x%03X rev=%u base=0x%08lX wrap=0x%08lX\r\n",
                     (unsigned int)i,
                     (unsigned int)probe.core_id[i],
                     (unsigned int)probe.core_rev[i],
                     (unsigned long)probe.core_base[i],
                     (unsigned long)probe.core_wrap[i]);
    }
    probe_printf("[brcm] passive status=%ld socram status=%ld core=0x%08lX ram_base=0x%08lX ram=%lu sr=%lu\r\n",
                 (long)probe.passive_status,
                 (long)probe.socram_status,
                 (unsigned long)probe.socram_base,
                 (unsigned long)probe.socram_ram_base,
                 (unsigned long)probe.socram_ram_size,
                 (unsigned long)probe.socram_sr_size);
    probe_printf("[brcm] ram rw status=%ld addr=0x%08lX before=0x%08lX after=0x%08lX\r\n",
                 (long)probe.ram_rw_status,
                 (unsigned long)probe.ram_rw_address,
                 (unsigned long)probe.ram_rw_before,
                 (unsigned long)probe.ram_rw_after);
    if(fw_status == 0)
        download_status = app_ap6212_download_firmware(&fw_info, &probe);
    else
        download_status = -1000;
    probe_printf("[fw] download result=%d\r\n", download_status);
    if(download_status != 0 && (probe.io_ready_after & 0x04U) == 0U)
        probe_write("[ap6212-sdio] F2 not ready before firmware: expected at this stage\r\n");
    probe_write("[ap6212-sdio] probe OK (bring-up sequence reached firmware stage)\r\n");
}

UINT app_ap6212_sdio_probe_init(void)
{
    if(g_ap6212_io_mutex_ready == 0U)
    {
        if(tx_mutex_create(&g_ap6212_io_mutex,
                           "AP6212 SDPCM IO",
                           TX_NO_INHERIT) != TX_SUCCESS)
        {
            return TX_MUTEX_ERROR;
        }
        g_ap6212_io_mutex_ready = 1U;
    }

    return tx_thread_create(&g_ap6212_sdio_probe_thread,
                            "AP6212 SDIO probe",
                            app_ap6212_sdio_probe_entry,
                            0U,
                            g_ap6212_sdio_probe_stack,
                            sizeof(g_ap6212_sdio_probe_stack),
                            APP_AP6212_SDIO_PROBE_THREAD_PRIO,
                            APP_AP6212_SDIO_PROBE_THREAD_PRIO,
                            TX_NO_TIME_SLICE,
                            TX_AUTO_START);
}
