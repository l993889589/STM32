#include "bsp_ap6212_sdio.h"

#include <string.h>

#include "main.h"

#define AP6212_SDIO_CMD_TIMEOUT_LOOPS     1000000U
#define AP6212_SDIO_DATA_TIMEOUT_LOOPS    2000000U
#define AP6212_SDIO_OCR_3V2_3V4          0x00300000U
#define AP6212_SDIO_OCR_2V7_3V6          0x00FF8000U
#define AP6212_SDIO_OCR_READY            0x80000000U
#define AP6212_SDIO_OCR_NUM_FUNCS_Pos    28U
#define AP6212_SDIO_OCR_NUM_FUNCS_Msk    (0x7UL << AP6212_SDIO_OCR_NUM_FUNCS_Pos)
#define AP6212_SDIO_OCR_MEM_PRESENT      0x08000000U
#define AP6212_SDIO_CLEAR_FLAGS          0x1DC007FFU
#define AP6212_SDIO_CCCR_IO_ENABLE       0x02U
#define AP6212_SDIO_CCCR_IO_READY        0x03U
#define AP6212_SDIO_CCCR_BUS_IF_CTRL     0x07U
#define AP6212_SDIO_CCCR_CIS_PTR         0x09U
#define AP6212_SDIO_FUNC1_SBADDRLOW      0x1000AU
#define AP6212_SDIO_FUNC1_SDIOPULLUP     0x1000FU
#define AP6212_SDIO_FUNC1_CHIPCLKCSR     0x1000EU
#define AP6212_SDIO_FUNC1_SLEEPCSR       0x1001FU
#define AP6212_SDIO_FUNC1_CHIPCLK_FORCE_ALP 0x01U
#define AP6212_SDIO_FUNC1_CHIPCLK_ALP_REQ 0x08U
#define AP6212_SDIO_FUNC1_CHIPCLK_ALP    0x08U
#define AP6212_SDIO_FUNC1_CHIPCLK_HT     0x10U
#define AP6212_SDIO_FUNC1_CHIPCLK_FORCE_HW_OFF 0x20U
#define AP6212_SDIO_FUNC1_CHIPCLK_ALP_AV 0x40U
#define AP6212_SDIO_FUNC1_CHIPCLK_HT_AV  0x80U
#define AP6212_SDIO_FUNC1_CHIPCLK_AVBITS \
    (AP6212_SDIO_FUNC1_CHIPCLK_ALP_AV | AP6212_SDIO_FUNC1_CHIPCLK_HT_AV)
#define AP6212_SDIO_FUNC1_SLEEPCSR_KSO   0x01U
#define AP6212_SDIO_FBR_BASE(function)   (0x100U * (uint32_t)(function))
#define AP6212_SDIO_FBR_STD_IF           0x00U
#define AP6212_SDIO_FBR_CIS_PTR          0x09U
#define AP6212_SDIO_TUPLE_NULL           0x00U
#define AP6212_SDIO_TUPLE_MANFID         0x20U
#define AP6212_SDIO_TUPLE_END            0xFFU
#define AP6212_SDIO_CMD53_BYTE_MAX       512U
#define AP6212_SDIO_RAM_TRANSFER_CHUNK   64U
#define AP6212_SDIO_WRITE_PREFILL_MIN    32U
#define AP6212_SDIO_SB_OFT_ADDR_MASK     0x00007FFFU
#define AP6212_SDIO_SB_OFT_ADDR_LIMIT    0x00008000U
#define AP6212_SDIO_SB_ACCESS_2_4B_FLAG  0x00008000U
#define AP6212_SDIO_SBWINDOW_MASK        0xFFFF8000U
#define AP6212_SDIO_CHIPCOMMON_BASE      0x18000000U
#define AP6212_SDIO_CHIPCOMMON_EROMPTR   0x000000FCU
#define AP6212_SDIO_SDPCM_INTSTATUS      0x00000020U
#define AP6212_SDIO_CORE_CHIPCOMMON      0x0800U
#define AP6212_SDIO_CORE_80211           0x0812U
#define AP6212_SDIO_CORE_INTERNAL_MEM    0x080EU
#define AP6212_SDIO_CORE_SDIO_DEV        0x0829U
#define AP6212_SDIO_CORE_PMU             0x0827U
#define AP6212_SDIO_CORE_ARM_CM3         0x082AU
#define AP6212_SDIO_CORE_GCI             0x0840U
#define AP6212_SDIO_BCMA_IOCTL           0x00000408U
#define AP6212_SDIO_BCMA_IOCTL_CLK       0x00000001U
#define AP6212_SDIO_BCMA_IOCTL_FGC       0x00000002U
#define AP6212_SDIO_BCMA_RESET_CTL       0x00000800U
#define AP6212_SDIO_BCMA_RESET_CTL_RESET 0x00000001U
#define AP6212_SDIO_D11_IOCTL_PHYCLOCKEN 0x00000004U
#define AP6212_SDIO_D11_IOCTL_PHYRESET   0x00000008U
#define AP6212_SDIO_DMP_DESC_TYPE_MSK    0x0000000FU
#define AP6212_SDIO_DMP_DESC_EMPTY       0x00000000U
#define AP6212_SDIO_DMP_DESC_VALID       0x00000001U
#define AP6212_SDIO_DMP_DESC_COMPONENT   0x00000001U
#define AP6212_SDIO_DMP_DESC_MASTER_PORT 0x00000003U
#define AP6212_SDIO_DMP_DESC_ADDRESS     0x00000005U
#define AP6212_SDIO_DMP_DESC_ADDRSIZE_GT32 0x00000008U
#define AP6212_SDIO_DMP_DESC_EOT         0x0000000FU
#define AP6212_SDIO_DMP_COMP_PARTNUM     0x000FFF00U
#define AP6212_SDIO_DMP_COMP_PARTNUM_S   8U
#define AP6212_SDIO_DMP_COMP_REVISION    0xFF000000U
#define AP6212_SDIO_DMP_COMP_REVISION_S  24U
#define AP6212_SDIO_DMP_COMP_NUM_SWRAP   0x00F80000U
#define AP6212_SDIO_DMP_COMP_NUM_SWRAP_S 19U
#define AP6212_SDIO_DMP_COMP_NUM_MWRAP   0x0007C000U
#define AP6212_SDIO_DMP_COMP_NUM_MWRAP_S 14U
#define AP6212_SDIO_DMP_SLAVE_ADDR_BASE  0xFFFFF000U
#define AP6212_SDIO_DMP_SLAVE_TYPE       0x000000C0U
#define AP6212_SDIO_DMP_SLAVE_TYPE_S     6U
#define AP6212_SDIO_DMP_SLAVE_TYPE_SLAVE 0U
#define AP6212_SDIO_DMP_SLAVE_TYPE_SWRAP 2U
#define AP6212_SDIO_DMP_SLAVE_TYPE_MWRAP 3U
#define AP6212_SDIO_DMP_SLAVE_SIZE_TYPE  0x00000030U
#define AP6212_SDIO_DMP_SLAVE_SIZE_TYPE_S 4U
#define AP6212_SDIO_DMP_SLAVE_SIZE_4K    0U
#define AP6212_SDIO_DMP_SLAVE_SIZE_8K    1U
#define AP6212_SDIO_DMP_SLAVE_SIZE_DESC  3U
#define AP6212_SDIO_SOCRAM_COREINFO      0x00000000U
#define AP6212_SDIO_SOCRAM_BANKIDX       0x00000010U
#define AP6212_SDIO_SOCRAM_BANKINFO      0x00000040U
#define AP6212_SDIO_SOCRAM_BANKPDA       0x00000044U
#define AP6212_SDIO_SOCRAM_BANKINFO_RETNTRAM_MASK 0x00010000U
#define AP6212_SDIO_SOCRAM_BANKINFO_SZMASK 0x0000007FU
#define AP6212_SDIO_SOCRAM_BANKIDX_MEMTYPE_SHIFT 8U
#define AP6212_SDIO_SOCRAM_MEMTYPE_RAM   0U
#define AP6212_SDIO_SOCRAM_BANKINFO_SZBASE 8192U
#define AP6212_SDIO_SRCI_LSS_MASK        0x00F00000U
#define AP6212_SDIO_SRCI_LSS_SHIFT       20U
#define AP6212_SDIO_SRCI_SRNB_MASK       0x000000F0U
#define AP6212_SDIO_SRCI_SRNB_MASK_EXT   0x00000100U
#define AP6212_SDIO_SRCI_SRNB_SHIFT      4U
#define AP6212_SDIO_SRCI_SRBSZ_MASK      0x0000000FU
#define AP6212_SDIO_SRCI_SRBSZ_SHIFT     0U
#define AP6212_SDIO_SR_BSZ_BASE          14U
#define AP6212_SDIO_DATA_ERROR_FLAGS \
    (SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | SDMMC_STA_TXUNDERR | \
     SDMMC_STA_RXOVERR | SDMMC_STA_DABORT | SDMMC_STA_IDMATE)

static int ap6212_sdio_send_cmd(uint8_t cmd_index,
                                uint32_t argument,
                                uint8_t response,
                                uint8_t ignore_crc,
                                uint32_t *response1,
                                uint32_t *status);

static bsp_ap6212_sdio_ext_debug_t g_ap6212_sdio_last_ext_debug;

static void ap6212_sdio_record_ext_debug(uint8_t write,
                                         uint8_t function,
                                         uint32_t address,
                                         uint8_t increment,
                                         uint32_t length,
                                         uint32_t transferred,
                                         uint32_t sta,
                                         uint32_t resp1,
                                         int32_t status)
{
    g_ap6212_sdio_last_ext_debug.sta = sta;
    g_ap6212_sdio_last_ext_debug.resp1 = resp1;
    g_ap6212_sdio_last_ext_debug.address = address;
    g_ap6212_sdio_last_ext_debug.length = length;
    g_ap6212_sdio_last_ext_debug.transferred = transferred;
    g_ap6212_sdio_last_ext_debug.status = status;
    g_ap6212_sdio_last_ext_debug.write = write;
    g_ap6212_sdio_last_ext_debug.function = function;
    g_ap6212_sdio_last_ext_debug.increment = increment;
}

void bsp_ap6212_sdio_get_last_ext_debug(bsp_ap6212_sdio_ext_debug_t *debug)
{
    if(debug != NULL)
        *debug = g_ap6212_sdio_last_ext_debug;
}

static int ap6212_sdio_wait_idle(void)
{
    uint32_t timeout = AP6212_SDIO_CMD_TIMEOUT_LOOPS;

    while((SDMMC2->STA & SDMMC_STA_CPSMACT) != 0U)
    {
        if(timeout-- == 0U)
            return -1;
    }

    return 0;
}

static uint32_t ap6212_sdio_cmd52_arg(uint8_t write,
                                      uint8_t function,
                                      uint32_t address,
                                      uint8_t data)
{
    return ((uint32_t)(write != 0U) << 31) |
           (((uint32_t)function & 0x7U) << 28) |
           ((address & 0x1FFFFU) << 9) |
           data;
}

static uint32_t ap6212_sdio_cmd53_arg(uint8_t write,
                                      uint8_t function,
                                      uint32_t address,
                                      uint8_t increment,
                                      uint32_t length)
{
    uint32_t count = (length == AP6212_SDIO_CMD53_BYTE_MAX) ? 0U : length;

    return ((uint32_t)(write != 0U) << 31) |
           (((uint32_t)function & 0x7U) << 28) |
           ((uint32_t)(increment != 0U) << 26) |
           ((address & 0x1FFFFU) << 9) |
           (count & 0x1FFU);
}

static int ap6212_sdio_wait_data_idle(void)
{
    uint32_t timeout = AP6212_SDIO_CMD_TIMEOUT_LOOPS;

    while((SDMMC2->STA & SDMMC_STA_DPSMACT) != 0U)
    {
        if(timeout-- == 0U)
            return -1;
    }

    return 0;
}

static void ap6212_sdio_reset_data_path(void)
{
    SDMMC2->IDMACTRL = 0U;
    SDMMC2->DCTRL = SDMMC_DCTRL_FIFORST;
    SDMMC2->DCTRL = 0U;
    SDMMC2->ICR = AP6212_SDIO_CLEAR_FLAGS;
}

int bsp_ap6212_sdio_readb(uint8_t function, uint32_t address, uint8_t *value)
{
    uint32_t response = 0U;

    if(value == NULL || function > 7U || address > 0x1FFFFU)
        return -1;

    if(ap6212_sdio_send_cmd(52U,
                            ap6212_sdio_cmd52_arg(0U, function, address, 0U),
                            1U,
                            0U,
                            &response,
                            NULL) != 0)
        return -2;

    *value = (uint8_t)(response & 0xFFU);
    return 0;
}

int bsp_ap6212_sdio_writeb(uint8_t function, uint32_t address, uint8_t value)
{
    uint32_t response = 0U;

    if(function > 7U || address > 0x1FFFFU)
        return -1;

    if(ap6212_sdio_send_cmd(52U,
                            ap6212_sdio_cmd52_arg(1U, function, address, value),
                            1U,
                            0U,
                            &response,
                            NULL) != 0)
        return -2;

    return (((uint8_t)(response & 0xFFU) == value) ? 0 : -3);
}

int bsp_ap6212_sdio_read_ext(uint8_t function,
                             uint32_t address,
                             uint8_t increment,
                             uint8_t *data,
                             uint32_t length,
                             uint32_t *last_sta)
{
    uint32_t flags = 0U;
    uint32_t response = 0U;
    uint32_t bytes_read = 0U;
    int32_t status = 0;
    uint8_t cmd_done = 0U;
    uint8_t data_done = 0U;

    if(data == NULL ||
       function > 7U ||
       address > 0x1FFFFU ||
       length == 0U ||
       length > AP6212_SDIO_CMD53_BYTE_MAX)
    {
        return -1;
    }

    if(ap6212_sdio_wait_idle() != 0 || ap6212_sdio_wait_data_idle() != 0)
        return -2;

    ap6212_sdio_reset_data_path();
    SDMMC2->DTIMER = 0xFFFFFFFFU;
    SDMMC2->DLEN = length;
    SDMMC2->DCTRL = SDMMC_DCTRL_DTDIR | SDMMC_DCTRL_DTMODE_0;
    SDMMC2->ARG = ap6212_sdio_cmd53_arg(0U, function, address, increment, length);
    SDMMC2->CMD = 53U |
                  SDMMC_CMD_WAITRESP_0 |
                  SDMMC_CMD_CMDTRANS |
                  SDMMC_CMD_CPSMEN;

    for(uint32_t timeout = 0U; timeout < AP6212_SDIO_DATA_TIMEOUT_LOOPS; timeout++)
    {
        flags = SDMMC2->STA;

        if((flags & AP6212_SDIO_DATA_ERROR_FLAGS) != 0U)
            break;

        if(cmd_done == 0U &&
           (flags & (SDMMC_STA_CMDREND | SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
        {
            cmd_done = 1U;
            if((flags & (SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
                break;
        }

        while(bytes_read < length && (SDMMC2->STA & SDMMC_STA_RXFIFOE) == 0U)
        {
            uint32_t word = SDMMC2->FIFO;

            for(uint32_t byte_index = 0U; byte_index < 4U && bytes_read < length; byte_index++)
            {
                data[bytes_read++] = (uint8_t)(word >> (byte_index * 8U));
            }
        }

        flags = SDMMC2->STA;
        if((flags & SDMMC_STA_DATAEND) != 0U)
            data_done = 1U;

        if(cmd_done != 0U && data_done != 0U && bytes_read >= length)
            break;
    }

    flags = SDMMC2->STA;
    response = SDMMC2->RESP1;
    if(last_sta != NULL)
        *last_sta = flags;

    SDMMC2->DCTRL = 0U;
    SDMMC2->CMD &= ~SDMMC_CMD_CMDTRANS;
    SDMMC2->ICR = AP6212_SDIO_CLEAR_FLAGS;

    if(cmd_done == 0U)
        status = -3;
    else if((flags & (SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
        status = -4;
    else if((flags & AP6212_SDIO_DATA_ERROR_FLAGS) != 0U)
        status = -5;
    else if(data_done == 0U)
        status = -6;
    else if(bytes_read != length)
        status = -7;

    ap6212_sdio_record_ext_debug(0U,
                                 function,
                                 address,
                                 increment,
                                 length,
                                 bytes_read,
                                 flags,
                                 response,
                                 status);

    return status;
}

int bsp_ap6212_sdio_write_ext(uint8_t function,
                              uint32_t address,
                              uint8_t increment,
                              const uint8_t *data,
                              uint32_t length,
                              uint32_t *last_sta)
{
    uint32_t flags = 0U;
    uint32_t response = 0U;
    uint32_t bytes_written = 0U;
    int32_t status = 0;
    uint8_t cmd_done = 0U;
    uint8_t data_done = 0U;

    if(data == NULL ||
       function > 7U ||
       address > 0x1FFFFU ||
       length == 0U ||
       length > AP6212_SDIO_CMD53_BYTE_MAX)
    {
        return -1;
    }

    if(ap6212_sdio_wait_idle() != 0 || ap6212_sdio_wait_data_idle() != 0)
        return -2;

    ap6212_sdio_reset_data_path();
    SDMMC2->DTIMER = 0xFFFFFFFFU;
    SDMMC2->DLEN = length;
    SDMMC2->DCTRL = SDMMC_DCTRL_DTMODE_0;
    SDMMC2->ARG = ap6212_sdio_cmd53_arg(1U, function, address, increment, length);
    SDMMC2->CMD = 53U |
                  SDMMC_CMD_WAITRESP_0 |
                  SDMMC_CMD_CMDTRANS |
                  SDMMC_CMD_CPSMEN;

    for(uint32_t timeout = 0U; timeout < AP6212_SDIO_DATA_TIMEOUT_LOOPS; timeout++)
    {
        flags = SDMMC2->STA;

        if((flags & AP6212_SDIO_DATA_ERROR_FLAGS) != 0U)
            break;

        if(cmd_done == 0U &&
           (flags & (SDMMC_STA_CMDREND | SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
        {
            cmd_done = 1U;
            if((flags & (SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
                break;
        }

        if(cmd_done != 0U && bytes_written < length)
        {
            uint32_t remaining = length - bytes_written;

            if((flags & SDMMC_STA_TXFIFOHE) != 0U && remaining >= 32U)
            {
                for(uint32_t word_index = 0U; word_index < 8U; word_index++)
                {
                    uint32_t word =
                        (uint32_t)data[bytes_written] |
                        ((uint32_t)data[bytes_written + 1U] << 8) |
                        ((uint32_t)data[bytes_written + 2U] << 16) |
                        ((uint32_t)data[bytes_written + 3U] << 24);
                    bytes_written += 4U;
                    SDMMC2->FIFO = word;
                }
            }
            else if((flags & (SDMMC_STA_TXFIFOHE | SDMMC_STA_TXFIFOE)) != 0U)
            {
                while(bytes_written < length)
                {
                    uint32_t word = 0U;

                    for(uint32_t byte_index = 0U;
                        byte_index < 4U && bytes_written < length;
                        byte_index++)
                    {
                        word |= ((uint32_t)data[bytes_written++]) << (byte_index * 8U);
                    }

                    SDMMC2->FIFO = word;
                }
            }
        }

        flags = SDMMC2->STA;
        if((flags & SDMMC_STA_DATAEND) != 0U)
            data_done = 1U;

        if(cmd_done != 0U && data_done != 0U)
            break;
    }

    flags = SDMMC2->STA;
    response = SDMMC2->RESP1;
    if(last_sta != NULL)
        *last_sta = flags;

    SDMMC2->DCTRL = 0U;
    SDMMC2->CMD &= ~SDMMC_CMD_CMDTRANS;
    SDMMC2->ICR = AP6212_SDIO_CLEAR_FLAGS;

    if(cmd_done == 0U)
        status = -3;
    else if((flags & (SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
        status = -4;
    else if((flags & AP6212_SDIO_DATA_ERROR_FLAGS) != 0U)
        status = -5;
    else if(data_done == 0U)
        status = -6;
    else if(bytes_written != length)
        status = -7;

    ap6212_sdio_record_ext_debug(1U,
                                 function,
                                 address,
                                 increment,
                                 length,
                                 bytes_written,
                                 flags,
                                 response,
                                 status);

    return status;
}

static int ap6212_sdio_read_u24(uint8_t function, uint32_t address, uint32_t *value)
{
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;

    if(value == NULL)
        return -1;
    if(bsp_ap6212_sdio_readb(function, address, &b0) != 0 ||
       bsp_ap6212_sdio_readb(function, address + 1U, &b1) != 0 ||
       bsp_ap6212_sdio_readb(function, address + 2U, &b2) != 0)
        return -2;

    *value = (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16);
    return 0;
}

static int ap6212_sdio_set_backplane_window(uint32_t address)
{
    uint32_t value = (address & AP6212_SDIO_SBWINDOW_MASK) >> 8;

    for(uint32_t i = 0U; i < 3U; i++)
    {
        if(bsp_ap6212_sdio_writeb(1U,
                                  AP6212_SDIO_FUNC1_SBADDRLOW + i,
                                  (uint8_t)(value & 0xFFU)) != 0)
        {
            return -1;
        }

        value >>= 8;
    }

    return 0;
}

static int ap6212_sdio_backplane_read32(uint32_t address,
                                        uint32_t *value,
                                        uint8_t *raw,
                                        uint32_t *last_sta)
{
    uint8_t bytes[4] = {0U, 0U, 0U, 0U};
    uint32_t sdio_address;
    int status;

    if(value == NULL)
        return -1;

    if(ap6212_sdio_set_backplane_window(address) != 0)
        return -2;

    sdio_address = (address & AP6212_SDIO_SB_OFT_ADDR_MASK) |
                   AP6212_SDIO_SB_ACCESS_2_4B_FLAG;
    status = bsp_ap6212_sdio_read_ext(1U,
                                      sdio_address,
                                      1U,
                                      bytes,
                                      sizeof(bytes),
                                      last_sta);
    if(raw != NULL)
        memcpy(raw, bytes, sizeof(bytes));
    if(status != 0)
        return status;

    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return 0;
}

static int ap6212_sdio_backplane_write32(uint32_t address, uint32_t value)
{
    uint8_t bytes[4];
    uint32_t sdio_address;
    uint32_t last_sta;

    if(ap6212_sdio_set_backplane_window(address) != 0)
        return -1;

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    sdio_address = (address & AP6212_SDIO_SB_OFT_ADDR_MASK) |
                   AP6212_SDIO_SB_ACCESS_2_4B_FLAG;

    return bsp_ap6212_sdio_write_ext(1U,
                                     sdio_address,
                                     1U,
                                     bytes,
                                     sizeof(bytes),
                                     &last_sta);
}

static int ap6212_sdio_dmp_get_desc(uint32_t *eromaddr,
                                    uint32_t *value,
                                    uint8_t *type)
{
    uint32_t desc_value = 0U;

    if(eromaddr == NULL || value == NULL)
        return -1;

    if(ap6212_sdio_backplane_read32(*eromaddr, &desc_value, NULL, NULL) != 0)
        return -2;

    *eromaddr += 4U;
    *value = desc_value;

    if(type != NULL)
    {
        uint8_t desc_type = (uint8_t)(desc_value & AP6212_SDIO_DMP_DESC_TYPE_MSK);
        if((desc_type & (uint8_t)~AP6212_SDIO_DMP_DESC_ADDRSIZE_GT32) ==
           AP6212_SDIO_DMP_DESC_ADDRESS)
        {
            desc_type = AP6212_SDIO_DMP_DESC_ADDRESS;
        }
        *type = desc_type;
    }

    return 0;
}

static int ap6212_sdio_dmp_get_regaddr(uint32_t *eromaddr,
                                       uint32_t *regbase,
                                       uint32_t *wrapbase)
{
    uint8_t desc;
    uint8_t wraptype;
    uint32_t value;

    if(eromaddr == NULL || regbase == NULL || wrapbase == NULL)
        return -1;

    *regbase = 0U;
    *wrapbase = 0U;

    if(ap6212_sdio_dmp_get_desc(eromaddr, &value, &desc) != 0)
        return -2;

    if(desc == AP6212_SDIO_DMP_DESC_MASTER_PORT)
    {
        wraptype = AP6212_SDIO_DMP_SLAVE_TYPE_MWRAP;
    }
    else if(desc == AP6212_SDIO_DMP_DESC_ADDRESS)
    {
        *eromaddr -= 4U;
        wraptype = AP6212_SDIO_DMP_SLAVE_TYPE_SWRAP;
    }
    else
    {
        *eromaddr -= 4U;
        return -3;
    }

    for(uint32_t guard = 0U; guard < 64U; guard++)
    {
        uint8_t stype;
        uint8_t sztype;

        do
        {
            if(ap6212_sdio_dmp_get_desc(eromaddr, &value, &desc) != 0)
                return -4;
            if(desc == AP6212_SDIO_DMP_DESC_EOT)
            {
                *eromaddr -= 4U;
                return -5;
            }
        } while(desc != AP6212_SDIO_DMP_DESC_ADDRESS &&
                desc != AP6212_SDIO_DMP_DESC_COMPONENT);

        if(desc == AP6212_SDIO_DMP_DESC_COMPONENT)
        {
            *eromaddr -= 4U;
            return 0;
        }

        if((value & AP6212_SDIO_DMP_DESC_ADDRSIZE_GT32) != 0U)
        {
            uint32_t ignored;
            if(ap6212_sdio_dmp_get_desc(eromaddr, &ignored, NULL) != 0)
                return -6;
        }

        sztype = (uint8_t)((value & AP6212_SDIO_DMP_SLAVE_SIZE_TYPE) >>
                           AP6212_SDIO_DMP_SLAVE_SIZE_TYPE_S);
        if(sztype == AP6212_SDIO_DMP_SLAVE_SIZE_DESC)
        {
            uint32_t ignored;
            if(ap6212_sdio_dmp_get_desc(eromaddr, &ignored, NULL) != 0)
                return -7;
            if((ignored & AP6212_SDIO_DMP_DESC_ADDRSIZE_GT32) != 0U &&
               ap6212_sdio_dmp_get_desc(eromaddr, &ignored, NULL) != 0)
            {
                return -8;
            }
        }

        if(sztype != AP6212_SDIO_DMP_SLAVE_SIZE_4K &&
           sztype != AP6212_SDIO_DMP_SLAVE_SIZE_8K)
        {
            continue;
        }

        stype = (uint8_t)((value & AP6212_SDIO_DMP_SLAVE_TYPE) >>
                          AP6212_SDIO_DMP_SLAVE_TYPE_S);
        if(*regbase == 0U && stype == AP6212_SDIO_DMP_SLAVE_TYPE_SLAVE)
            *regbase = value & AP6212_SDIO_DMP_SLAVE_ADDR_BASE;
        if(*wrapbase == 0U && stype == wraptype)
            *wrapbase = value & AP6212_SDIO_DMP_SLAVE_ADDR_BASE;

        if(*regbase != 0U && *wrapbase != 0U)
            return 0;
    }

    return -9;
}

static int ap6212_sdio_scan_cores(bsp_ap6212_sdio_probe_t *probe)
{
    uint8_t desc_type = 0U;
    uint32_t eromaddr = 0U;
    uint32_t value = 0U;

    if(probe == NULL)
        return -1;

    if(ap6212_sdio_backplane_read32(AP6212_SDIO_CHIPCOMMON_BASE +
                                    AP6212_SDIO_CHIPCOMMON_EROMPTR,
                                    &eromaddr,
                                    NULL,
                                    NULL) != 0)
    {
        return -2;
    }

    probe->erom_ptr = eromaddr;
    probe->core_count = 0U;

    for(uint32_t guard = 0U; guard < 128U && desc_type != AP6212_SDIO_DMP_DESC_EOT; guard++)
    {
        uint16_t id;
        uint8_t nmw;
        uint8_t nsw;
        uint8_t rev;
        uint32_t base = 0U;
        uint32_t wrap = 0U;

        if(ap6212_sdio_dmp_get_desc(&eromaddr, &value, &desc_type) != 0)
            return -3;

        if((value & AP6212_SDIO_DMP_DESC_VALID) == 0U ||
           desc_type == AP6212_SDIO_DMP_DESC_EMPTY)
        {
            continue;
        }
        if(desc_type != AP6212_SDIO_DMP_DESC_COMPONENT)
            continue;

        id = (uint16_t)((value & AP6212_SDIO_DMP_COMP_PARTNUM) >>
                        AP6212_SDIO_DMP_COMP_PARTNUM_S);

        if(ap6212_sdio_dmp_get_desc(&eromaddr, &value, &desc_type) != 0)
            return -4;
        if((value & AP6212_SDIO_DMP_DESC_TYPE_MSK) != AP6212_SDIO_DMP_DESC_COMPONENT)
            return -5;

        nmw = (uint8_t)((value & AP6212_SDIO_DMP_COMP_NUM_MWRAP) >>
                        AP6212_SDIO_DMP_COMP_NUM_MWRAP_S);
        nsw = (uint8_t)((value & AP6212_SDIO_DMP_COMP_NUM_SWRAP) >>
                        AP6212_SDIO_DMP_COMP_NUM_SWRAP_S);
        rev = (uint8_t)((value & AP6212_SDIO_DMP_COMP_REVISION) >>
                        AP6212_SDIO_DMP_COMP_REVISION_S);

        if(nmw + nsw == 0U &&
           id != AP6212_SDIO_CORE_PMU &&
           id != AP6212_SDIO_CORE_GCI)
        {
            continue;
        }

        if(ap6212_sdio_dmp_get_regaddr(&eromaddr, &base, &wrap) != 0)
            continue;

        if(probe->core_count < BSP_AP6212_SDIO_CORE_SCAN_MAX)
        {
            uint8_t index = probe->core_count++;
            probe->core_id[index] = id;
            probe->core_rev[index] = rev;
            probe->core_base[index] = base;
            probe->core_wrap[index] = wrap;
        }
    }

    return (probe->core_count > 0U) ? 0 : -6;
}

static int ap6212_sdio_find_core(const bsp_ap6212_sdio_probe_t *probe,
                                 uint16_t id,
                                 uint32_t *base,
                                 uint32_t *wrap)
{
    if(probe == NULL)
        return -1;

    for(uint8_t i = 0U; i < probe->core_count; i++)
    {
        if(probe->core_id[i] == id)
        {
            if(base != NULL)
                *base = probe->core_base[i];
            if(wrap != NULL)
                *wrap = probe->core_wrap[i];
            return 0;
        }
    }

    return -2;
}

static int ap6212_sdio_ai_coredisable(uint32_t wrapbase,
                                      uint32_t prereset,
                                      uint32_t reset)
{
    uint32_t regdata = 0U;

    if(wrapbase == 0U)
        return -1;

    if(ap6212_sdio_backplane_read32(wrapbase + AP6212_SDIO_BCMA_RESET_CTL,
                                    &regdata,
                                    NULL,
                                    NULL) != 0)
    {
        return -2;
    }

    if((regdata & AP6212_SDIO_BCMA_RESET_CTL_RESET) == 0U)
    {
        if(ap6212_sdio_backplane_write32(wrapbase + AP6212_SDIO_BCMA_IOCTL,
                                         prereset |
                                         AP6212_SDIO_BCMA_IOCTL_FGC |
                                         AP6212_SDIO_BCMA_IOCTL_CLK) != 0)
            return -3;
        (void)ap6212_sdio_backplane_read32(wrapbase + AP6212_SDIO_BCMA_IOCTL,
                                           &regdata,
                                           NULL,
                                           NULL);

        if(ap6212_sdio_backplane_write32(wrapbase + AP6212_SDIO_BCMA_RESET_CTL,
                                         AP6212_SDIO_BCMA_RESET_CTL_RESET) != 0)
            return -4;
        HAL_Delay(1U);

        for(uint32_t retry = 0U; retry < 50U; retry++)
        {
            if(ap6212_sdio_backplane_read32(wrapbase + AP6212_SDIO_BCMA_RESET_CTL,
                                            &regdata,
                                            NULL,
                                            NULL) != 0)
                return -5;
            if((regdata & AP6212_SDIO_BCMA_RESET_CTL_RESET) != 0U)
                break;
        }
    }

    if(ap6212_sdio_backplane_write32(wrapbase + AP6212_SDIO_BCMA_IOCTL,
                                     reset |
                                     AP6212_SDIO_BCMA_IOCTL_FGC |
                                     AP6212_SDIO_BCMA_IOCTL_CLK) != 0)
        return -6;
    (void)ap6212_sdio_backplane_read32(wrapbase + AP6212_SDIO_BCMA_IOCTL,
                                       &regdata,
                                       NULL,
                                       NULL);

    return 0;
}

static int ap6212_sdio_ai_resetcore(uint32_t wrapbase,
                                    uint32_t prereset,
                                    uint32_t reset,
                                    uint32_t postreset)
{
    uint32_t regdata = 0U;
    int status;

    status = ap6212_sdio_ai_coredisable(wrapbase, prereset, reset);
    if(status != 0)
        return status;

    for(uint32_t retry = 0U; retry < 50U; retry++)
    {
        if(ap6212_sdio_backplane_read32(wrapbase + AP6212_SDIO_BCMA_RESET_CTL,
                                        &regdata,
                                        NULL,
                                        NULL) != 0)
            return -10;
        if((regdata & AP6212_SDIO_BCMA_RESET_CTL_RESET) == 0U)
            break;

        if(ap6212_sdio_backplane_write32(wrapbase + AP6212_SDIO_BCMA_RESET_CTL,
                                         0U) != 0)
            return -11;
        HAL_Delay(1U);
    }

    if(ap6212_sdio_backplane_write32(wrapbase + AP6212_SDIO_BCMA_IOCTL,
                                     postreset | AP6212_SDIO_BCMA_IOCTL_CLK) != 0)
        return -12;
    (void)ap6212_sdio_backplane_read32(wrapbase + AP6212_SDIO_BCMA_IOCTL,
                                       &regdata,
                                       NULL,
                                       NULL);

    return 0;
}

static int ap6212_sdio_set_passive(bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t arm_wrap = 0U;
    uint32_t d11_wrap = 0U;
    uint32_t socram_base = 0U;
    uint32_t socram_wrap = 0U;
    int status;

    if(probe == NULL)
        return -1;

    if(ap6212_sdio_find_core(probe, AP6212_SDIO_CORE_ARM_CM3, NULL, &arm_wrap) == 0)
    {
        status = ap6212_sdio_ai_coredisable(arm_wrap, 0U, 0U);
        if(status != 0)
            return -10 + status;
    }

    if(ap6212_sdio_find_core(probe, AP6212_SDIO_CORE_80211, NULL, &d11_wrap) == 0)
    {
        status = ap6212_sdio_ai_resetcore(d11_wrap,
                                          AP6212_SDIO_D11_IOCTL_PHYRESET |
                                          AP6212_SDIO_D11_IOCTL_PHYCLOCKEN,
                                          AP6212_SDIO_D11_IOCTL_PHYCLOCKEN,
                                          AP6212_SDIO_D11_IOCTL_PHYCLOCKEN);
        if(status != 0)
            return -100 + status;
    }

    if(ap6212_sdio_find_core(probe,
                             AP6212_SDIO_CORE_INTERNAL_MEM,
                             &socram_base,
                             &socram_wrap) == 0)
    {
        status = ap6212_sdio_ai_resetcore(socram_wrap, 0U, 0U, 0U);
        if(status != 0)
            return -200 + status;

        if((probe->cmd53_backplane_chipid & 0xFFFFU) == 0xA9A6U)
        {
            (void)ap6212_sdio_backplane_write32(socram_base +
                                                AP6212_SDIO_SOCRAM_BANKIDX,
                                                3U);
            (void)ap6212_sdio_backplane_write32(socram_base +
                                                AP6212_SDIO_SOCRAM_BANKPDA,
                                                0U);
        }
    }

    return 0;
}

static int ap6212_sdio_socram_info(bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t coreinfo;
    uint32_t ram_size = 0U;
    uint32_t sr_size = 0U;
    uint8_t rev = 0U;
    uint32_t base = 0U;

    if(probe == NULL)
        return -1;

    for(uint8_t i = 0U; i < probe->core_count; i++)
    {
        if(probe->core_id[i] == AP6212_SDIO_CORE_INTERNAL_MEM)
        {
            rev = probe->core_rev[i];
            base = probe->core_base[i];
            break;
        }
    }

    if(base == 0U)
        return -2;

    probe->socram_base = base;
    if(ap6212_sdio_backplane_read32(base + AP6212_SDIO_SOCRAM_COREINFO,
                                    &coreinfo,
                                    NULL,
                                    NULL) != 0)
    {
        return -3;
    }

    if(rev <= 7U)
    {
        uint32_t nb = (coreinfo & AP6212_SDIO_SRCI_SRNB_MASK) >>
                      AP6212_SDIO_SRCI_SRNB_SHIFT;
        uint32_t banksize = (coreinfo & AP6212_SDIO_SRCI_SRBSZ_MASK) >>
                            AP6212_SDIO_SRCI_SRBSZ_SHIFT;
        uint32_t lss = (coreinfo & AP6212_SDIO_SRCI_LSS_MASK) >>
                       AP6212_SDIO_SRCI_LSS_SHIFT;

        ram_size = nb * (1UL << (banksize + AP6212_SDIO_SR_BSZ_BASE));
        if(lss != 0U)
            ram_size += (1UL << ((lss - 1U) + AP6212_SDIO_SR_BSZ_BASE));
    }
    else
    {
        uint32_t nb;

        if(rev >= 23U)
        {
            nb = (coreinfo & (AP6212_SDIO_SRCI_SRNB_MASK |
                              AP6212_SDIO_SRCI_SRNB_MASK_EXT)) >>
                 AP6212_SDIO_SRCI_SRNB_SHIFT;
        }
        else
        {
            nb = (coreinfo & AP6212_SDIO_SRCI_SRNB_MASK) >>
                 AP6212_SDIO_SRCI_SRNB_SHIFT;
        }

        for(uint32_t i = 0U; i < nb && i < 64U; i++)
        {
            uint32_t bankidx = i |
                               (AP6212_SDIO_SOCRAM_MEMTYPE_RAM <<
                                AP6212_SDIO_SOCRAM_BANKIDX_MEMTYPE_SHIFT);
            uint32_t bankinfo;
            uint32_t banksize;

            if(ap6212_sdio_backplane_write32(base + AP6212_SDIO_SOCRAM_BANKIDX,
                                             bankidx) != 0)
            {
                return -4;
            }
            if(ap6212_sdio_backplane_read32(base + AP6212_SDIO_SOCRAM_BANKINFO,
                                            &bankinfo,
                                            NULL,
                                            NULL) != 0)
            {
                return -5;
            }

            banksize = (bankinfo & AP6212_SDIO_SOCRAM_BANKINFO_SZMASK) + 1U;
            banksize *= AP6212_SDIO_SOCRAM_BANKINFO_SZBASE;
            ram_size += banksize;
            if((bankinfo & AP6212_SDIO_SOCRAM_BANKINFO_RETNTRAM_MASK) != 0U)
                sr_size += banksize;
        }
    }

    probe->socram_ram_base = 0U;
    probe->socram_ram_size = ram_size;
    probe->socram_sr_size = sr_size;

    return (ram_size != 0U) ? 0 : -6;
}

static int ap6212_sdio_ram_rw_smoke(bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t address;
    uint32_t before = 0U;
    uint32_t after = 0U;
    uint32_t restore = 0U;
    uint32_t pattern = 0xA55A3CC3U;

    if(probe == NULL || probe->socram_ram_size < 64U)
        return -1;

    address = probe->socram_ram_base + probe->socram_ram_size - 16U;
    probe->ram_rw_address = address;

    if(ap6212_sdio_backplane_read32(address, &before, NULL, NULL) != 0)
        return -2;
    probe->ram_rw_before = before;

    if(before == pattern)
        pattern = 0x5AA5C33CU;

    if(ap6212_sdio_backplane_write32(address, pattern) != 0)
        return -3;

    if(ap6212_sdio_backplane_read32(address, &after, NULL, NULL) != 0)
        return -4;
    probe->ram_rw_after = after;

    restore = ap6212_sdio_backplane_write32(address, before);
    if(restore != 0U)
        return -5;

    return (after == pattern) ? 0 : -6;
}

static int ap6212_sdio_ram_transfer(uint8_t write,
                                    uint32_t address,
                                    uint8_t *data,
                                    uint32_t length)
{
    uint32_t offset = 0U;

    if(data == NULL || length == 0U)
        return -1;

    while(offset < length)
    {
        uint32_t remaining = length - offset;
        uint32_t window_remaining =
            AP6212_SDIO_SB_OFT_ADDR_LIMIT -
            ((address + offset) & AP6212_SDIO_SB_OFT_ADDR_MASK);
        uint32_t chunk = remaining;
        uint32_t sdio_address;
        uint32_t last_sta;
        int status;

        if(chunk > AP6212_SDIO_RAM_TRANSFER_CHUNK)
            chunk = AP6212_SDIO_RAM_TRANSFER_CHUNK;
        if(chunk > window_remaining)
            chunk = window_remaining;

        if(ap6212_sdio_set_backplane_window(address + offset) != 0)
            return -2;

        sdio_address = ((address + offset) & AP6212_SDIO_SB_OFT_ADDR_MASK) |
                       AP6212_SDIO_SB_ACCESS_2_4B_FLAG;
        if(write != 0U)
        {
            status = bsp_ap6212_sdio_write_ext(1U,
                                               sdio_address,
                                               1U,
                                               data + offset,
                                               chunk,
                                               &last_sta);
        }
        else
        {
            status = bsp_ap6212_sdio_read_ext(1U,
                                              sdio_address,
                                              1U,
                                              data + offset,
                                              chunk,
                                              &last_sta);
        }

        if(status != 0)
            return status;

        offset += chunk;
    }

    return 0;
}

int bsp_ap6212_sdio_ram_read(uint32_t address, uint8_t *data, uint32_t length)
{
    return ap6212_sdio_ram_transfer(0U, address, data, length);
}

int bsp_ap6212_sdio_ram_write(uint32_t address, const uint8_t *data, uint32_t length)
{
    return ap6212_sdio_ram_transfer(1U, address, (uint8_t *)data, length);
}

int bsp_ap6212_sdio_release_cm3(const bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t arm_wrap = 0U;
    uint32_t sdio_base = 0U;
    uint32_t ignored = 0U;

    if(probe == NULL)
        return -1;

    if(ap6212_sdio_find_core(probe, AP6212_SDIO_CORE_SDIO_DEV, &sdio_base, NULL) != 0)
        return -2;
    if(ap6212_sdio_find_core(probe, AP6212_SDIO_CORE_ARM_CM3, NULL, &arm_wrap) != 0)
        return -3;

    (void)ap6212_sdio_backplane_write32(sdio_base + AP6212_SDIO_SDPCM_INTSTATUS,
                                        0xFFFFFFFFU);
    (void)ap6212_sdio_backplane_read32(sdio_base + AP6212_SDIO_SDPCM_INTSTATUS,
                                       &ignored,
                                       NULL,
                                       NULL);

    return ap6212_sdio_ai_resetcore(arm_wrap, 0U, 0U, 0U);
}

int bsp_ap6212_sdio_enable_f2(uint8_t *io_enable, uint8_t *io_ready)
{
    uint8_t enable = 0x06U;
    uint8_t ready = 0U;

    if(bsp_ap6212_sdio_writeb(0U, AP6212_SDIO_CCCR_IO_ENABLE, enable) != 0)
        return -1;

    for(uint32_t retry = 0U; retry < 500U; retry++)
    {
        if(bsp_ap6212_sdio_readb(0U, AP6212_SDIO_CCCR_IO_READY, &ready) != 0)
            return -2;
        if((ready & enable) == enable)
            break;
        HAL_Delay(1U);
    }

    if(io_enable != NULL)
        (void)bsp_ap6212_sdio_readb(0U, AP6212_SDIO_CCCR_IO_ENABLE, io_enable);
    if(io_ready != NULL)
        *io_ready = ready;

    return ((ready & enable) == enable) ? 0 : -3;
}

static void ap6212_sdio_read_manfid(uint32_t cis_ptr, uint16_t *manf, uint16_t *card)
{
    uint32_t offset = 0U;

    if(manf == NULL || card == NULL)
        return;

    *manf = 0U;
    *card = 0U;
    while(offset < 256U)
    {
        uint8_t tuple = 0U;
        uint8_t link = 0U;
        uint8_t data[4] = {0U, 0U, 0U, 0U};

        if(bsp_ap6212_sdio_readb(0U, cis_ptr + offset, &tuple) != 0)
            return;
        offset++;

        if(tuple == AP6212_SDIO_TUPLE_END)
            return;
        if(tuple == AP6212_SDIO_TUPLE_NULL)
            continue;

        if(bsp_ap6212_sdio_readb(0U, cis_ptr + offset, &link) != 0)
            return;
        offset++;

        if(tuple == AP6212_SDIO_TUPLE_MANFID && link >= 4U)
        {
            for(uint32_t i = 0U; i < 4U; i++)
            {
                if(bsp_ap6212_sdio_readb(0U, cis_ptr + offset + i, &data[i]) != 0)
                    return;
            }

            *manf = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            *card = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
            return;
        }

        offset += link;
    }
}

static int ap6212_sdio_buscore_prepare(bsp_ap6212_sdio_probe_t *probe)
{
    uint8_t clkset;
    uint8_t clkval = 0U;

    if(probe == NULL)
        return -1;

    clkset = AP6212_SDIO_FUNC1_CHIPCLK_FORCE_HW_OFF |
             AP6212_SDIO_FUNC1_CHIPCLK_ALP_REQ;
    if(bsp_ap6212_sdio_writeb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, clkset) != 0)
        return -2;
    if(bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, &clkval) != 0)
        return -3;

    probe->chipclk_alp_req = clkval;
    if((clkval & (uint8_t)~AP6212_SDIO_FUNC1_CHIPCLK_AVBITS) != clkset)
        return -4;

    for(uint32_t retry = 0U; retry < 20U; retry++)
    {
        if(bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, &clkval) != 0)
            return -5;
        probe->chipclk_alp_req = clkval;
        if((clkval & AP6212_SDIO_FUNC1_CHIPCLK_ALP_AV) != 0U)
            break;
        HAL_Delay(1U);
    }

    if((probe->chipclk_alp_req & AP6212_SDIO_FUNC1_CHIPCLK_ALP_AV) == 0U)
        return -6;

    clkset = AP6212_SDIO_FUNC1_CHIPCLK_FORCE_HW_OFF |
             AP6212_SDIO_FUNC1_CHIPCLK_FORCE_ALP;
    if(bsp_ap6212_sdio_writeb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, clkset) != 0)
        return -7;
    HAL_Delay(1U);
    if(bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, &probe->chipclk_force_alp) != 0)
        return -8;

    (void)bsp_ap6212_sdio_writeb(1U, AP6212_SDIO_FUNC1_SDIOPULLUP, 0U);
    (void)bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_SDIOPULLUP, &probe->sdio_pullup_after);

    return 0;
}

static int ap6212_sdio_send_cmd(uint8_t cmd_index,
                                uint32_t argument,
                                uint8_t response,
                                uint8_t ignore_crc,
                                uint32_t *response1,
                                uint32_t *status)
{
    uint32_t flags = 0U;
    uint8_t done = 0U;

    if(ap6212_sdio_wait_idle() != 0)
        return -1;

    SDMMC2->ICR = AP6212_SDIO_CLEAR_FLAGS;
    SDMMC2->ARG = argument;
    SDMMC2->CMD = ((uint32_t)cmd_index & SDMMC_CMD_CMDINDEX) |
                  (response != 0U ? SDMMC_CMD_WAITRESP_0 : 0U) |
                  SDMMC_CMD_CPSMEN;

    for(uint32_t timeout = 0U; timeout < AP6212_SDIO_CMD_TIMEOUT_LOOPS; timeout++)
    {
        flags = SDMMC2->STA;
        if(response == 0U)
        {
            if((flags & SDMMC_STA_CMDSENT) != 0U)
            {
                done = 1U;
                break;
            }
        }
        else
        {
            if((flags & (SDMMC_STA_CMDREND | SDMMC_STA_CCRCFAIL | SDMMC_STA_CTIMEOUT)) != 0U)
            {
                done = 1U;
                break;
            }
        }
    }

    flags = SDMMC2->STA;
    if(response1 != NULL)
        *response1 = SDMMC2->RESP1;
    if(status != NULL)
        *status = flags;

    SDMMC2->ICR = AP6212_SDIO_CLEAR_FLAGS;

    if(done == 0U)
        return -1;
    if((flags & SDMMC_STA_CTIMEOUT) != 0U)
        return -2;
    if(response != 0U && ignore_crc == 0U && (flags & SDMMC_STA_CCRCFAIL) != 0U)
        return -3;

    return 0;
}

bsp_ap6212_sdio_status_t bsp_ap6212_sdio_probe(bsp_ap6212_sdio_probe_t *probe)
{
    uint32_t response = 0U;
    uint32_t status = 0U;
    uint32_t ocr_arg = AP6212_SDIO_OCR_2V7_3V6;

    if(probe == NULL)
        return BSP_AP6212_SDIO_ERR_PARAM;

    memset(probe, 0, sizeof(*probe));

    if(ap6212_sdio_send_cmd(0U, 0U, 0U, 0U, NULL, &status) != 0)
    {
        probe->last_sta = status;
        return BSP_AP6212_SDIO_ERR_CMD0;
    }

    if(ap6212_sdio_send_cmd(5U, 0U, 1U, 1U, &response, &status) != 0)
    {
        probe->last_sta = status;
        return BSP_AP6212_SDIO_ERR_CMD5;
    }

    probe->cmd5_initial = response;
    if((response & 0x00FFFFFFU) != 0U)
        ocr_arg = response & 0x00FFFFFFU;
    else
        ocr_arg = AP6212_SDIO_OCR_3V2_3V4;

    for(uint32_t retry = 0U; retry < 100U; retry++)
    {
        if(ap6212_sdio_send_cmd(5U, ocr_arg, 1U, 1U, &response, &status) != 0)
        {
            probe->last_sta = status;
            return BSP_AP6212_SDIO_ERR_CMD5;
        }

        probe->cmd5_ready = response;
        if((response & AP6212_SDIO_OCR_READY) != 0U)
            break;

        HAL_Delay(1U);
    }

    probe->last_sta = status;
    probe->ocr = response & 0x00FFFFFFU;
    probe->io_functions = (uint8_t)((response & AP6212_SDIO_OCR_NUM_FUNCS_Msk) >>
                                    AP6212_SDIO_OCR_NUM_FUNCS_Pos);
    probe->memory_present = ((response & AP6212_SDIO_OCR_MEM_PRESENT) != 0U) ? 1U : 0U;

    if((response & AP6212_SDIO_OCR_READY) == 0U)
        return BSP_AP6212_SDIO_ERR_NOT_READY;

    if(ap6212_sdio_send_cmd(3U, 0U, 1U, 0U, &response, &status) != 0)
    {
        probe->last_sta = status;
        return BSP_AP6212_SDIO_ERR_CMD3;
    }

    probe->last_sta = status;
    probe->rca = (uint16_t)(response >> 16);

    if(ap6212_sdio_send_cmd(7U, ((uint32_t)probe->rca) << 16, 1U, 0U, &response, &status) != 0)
    {
        probe->last_sta = status;
        return BSP_AP6212_SDIO_ERR_CMD7;
    }

    for(uint32_t i = 0U; i < sizeof(probe->cccr); i++)
    {
        if(bsp_ap6212_sdio_readb(0U, i, &probe->cccr[i]) != 0)
            return BSP_AP6212_SDIO_ERR_CMD52;
    }

    if(ap6212_sdio_read_u24(0U, AP6212_SDIO_CCCR_CIS_PTR, &probe->common_cis_ptr) != 0)
        return BSP_AP6212_SDIO_ERR_CMD52;
    ap6212_sdio_read_manfid(probe->common_cis_ptr, &probe->common_manf, &probe->common_card);

    for(uint8_t function = 1U; function <= 2U; function++)
    {
        uint32_t index = (uint32_t)function - 1U;
        uint32_t fbr = AP6212_SDIO_FBR_BASE(function);

        (void)bsp_ap6212_sdio_readb(0U,
                                    fbr + AP6212_SDIO_FBR_STD_IF,
                                    &probe->func_code[index]);
        (void)ap6212_sdio_read_u24(0U,
                                   fbr + AP6212_SDIO_FBR_CIS_PTR,
                                   &probe->func_cis_ptr[index]);
        ap6212_sdio_read_manfid(probe->func_cis_ptr[index],
                                &probe->func_manf[index],
                                &probe->func_card[index]);
    }

    if(bsp_ap6212_sdio_readb(0U, AP6212_SDIO_CCCR_IO_ENABLE, &probe->io_enable_before) != 0 ||
       bsp_ap6212_sdio_readb(0U, AP6212_SDIO_CCCR_IO_READY, &probe->io_ready_before) != 0)
        return BSP_AP6212_SDIO_ERR_CMD52;

    if(probe->io_functions >= 1U)
    {
        uint8_t enable_mask = 0x02U;
        if(bsp_ap6212_sdio_writeb(0U, AP6212_SDIO_CCCR_IO_ENABLE, enable_mask) != 0)
            return BSP_AP6212_SDIO_ERR_CMD52;

        for(uint32_t retry = 0U; retry < 100U; retry++)
        {
            if(bsp_ap6212_sdio_readb(0U, AP6212_SDIO_CCCR_IO_READY, &probe->io_ready_after) != 0)
                return BSP_AP6212_SDIO_ERR_CMD52;
            if((probe->io_ready_after & enable_mask) == enable_mask)
                break;
            HAL_Delay(1U);
        }

        if(bsp_ap6212_sdio_readb(0U, AP6212_SDIO_CCCR_IO_ENABLE, &probe->io_enable_after) != 0)
            return BSP_AP6212_SDIO_ERR_CMD52;
    }

    (void)bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_SLEEPCSR, &probe->sleepcsr_before);
    (void)bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, &probe->chipclk_before);
    (void)bsp_ap6212_sdio_writeb(1U, AP6212_SDIO_FUNC1_SLEEPCSR, AP6212_SDIO_FUNC1_SLEEPCSR_KSO);
    (void)bsp_ap6212_sdio_writeb(1U,
                                 AP6212_SDIO_FUNC1_CHIPCLKCSR,
                                 AP6212_SDIO_FUNC1_CHIPCLK_HT);

    for(uint32_t retry = 0U; retry < 100U; retry++)
    {
        if(bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_CHIPCLKCSR, &probe->chipclk_after) != 0)
            break;
        if((probe->chipclk_after & AP6212_SDIO_FUNC1_CHIPCLK_HT_AV) != 0U)
            break;
        HAL_Delay(1U);
    }

    (void)bsp_ap6212_sdio_readb(1U, AP6212_SDIO_FUNC1_SLEEPCSR, &probe->sleepcsr_after);
    probe->buscore_prepare_status = ap6212_sdio_buscore_prepare(probe);
    (void)bsp_ap6212_sdio_readb(1U,
                                AP6212_SDIO_FUNC1_CHIPCLKCSR,
                                &probe->cmd53_cmd52_chipclk);
    probe->cmd53_smoke_status =
        ap6212_sdio_backplane_read32(AP6212_SDIO_CHIPCOMMON_BASE,
                                     &probe->cmd53_backplane_chipid,
                                     probe->cmd53_bytes,
                                     &probe->cmd53_last_sta);
    probe->cmd53_resp1 = SDMMC2->RESP1;
    probe->core_scan_status = ap6212_sdio_scan_cores(probe);
    if(probe->core_scan_status == 0)
    {
        probe->passive_status = ap6212_sdio_set_passive(probe);
        probe->socram_status = ap6212_sdio_socram_info(probe);
        if(probe->socram_status == 0)
            probe->ram_rw_status = ap6212_sdio_ram_rw_smoke(probe);
        else
            probe->ram_rw_status = -1000;
    }
    else
    {
        probe->passive_status = -1000;
        probe->socram_status = -1000;
        probe->ram_rw_status = -1000;
    }
    probe->wifi_host_wake =
        (HAL_GPIO_ReadPin(WIFI_HOST_WAKE_GPIO_Port, WIFI_HOST_WAKE_Pin) == GPIO_PIN_SET) ? 1U : 0U;

    return BSP_AP6212_SDIO_OK;
}
