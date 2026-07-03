#ifndef BSP_AP6212_SDIO_H
#define BSP_AP6212_SDIO_H

#include <stdint.h>

#define BSP_AP6212_SDIO_CORE_SCAN_MAX 12U

typedef struct
{
    uint32_t cmd5_initial;
    uint32_t cmd5_ready;
    uint32_t ocr;
    uint32_t last_sta;
    uint32_t common_cis_ptr;
    uint32_t func_cis_ptr[2];
    uint16_t common_manf;
    uint16_t common_card;
    uint16_t func_manf[2];
    uint16_t func_card[2];
    uint16_t rca;
    uint8_t io_functions;
    uint8_t memory_present;
    uint8_t cccr[16];
    uint8_t func_code[2];
    uint8_t io_enable_before;
    uint8_t io_ready_before;
    uint8_t io_enable_after;
    uint8_t io_ready_after;
    uint8_t sleepcsr_before;
    uint8_t sleepcsr_after;
    uint8_t chipclk_before;
    uint8_t chipclk_after;
    uint8_t chipclk_alp_req;
    uint8_t chipclk_force_alp;
    uint8_t sdio_pullup_after;
    uint8_t wifi_host_wake;
    int32_t buscore_prepare_status;
    int32_t cmd53_smoke_status;
    uint32_t cmd53_last_sta;
    uint32_t cmd53_resp1;
    uint32_t cmd53_backplane_chipid;
    uint32_t erom_ptr;
    uint32_t socram_base;
    uint32_t socram_ram_base;
    uint32_t socram_ram_size;
    uint32_t socram_sr_size;
    int32_t core_scan_status;
    int32_t passive_status;
    int32_t socram_status;
    int32_t ram_rw_status;
    uint32_t ram_rw_address;
    uint32_t ram_rw_before;
    uint32_t ram_rw_after;
    uint8_t core_count;
    uint16_t core_id[BSP_AP6212_SDIO_CORE_SCAN_MAX];
    uint8_t core_rev[BSP_AP6212_SDIO_CORE_SCAN_MAX];
    uint32_t core_base[BSP_AP6212_SDIO_CORE_SCAN_MAX];
    uint32_t core_wrap[BSP_AP6212_SDIO_CORE_SCAN_MAX];
    uint8_t cmd53_cmd52_chipclk;
    uint8_t cmd53_bytes[4];
} bsp_ap6212_sdio_probe_t;

typedef struct
{
    uint32_t sta;
    uint32_t resp1;
    uint32_t address;
    uint32_t length;
    uint32_t transferred;
    int32_t status;
    uint8_t write;
    uint8_t function;
    uint8_t increment;
} bsp_ap6212_sdio_ext_debug_t;

typedef enum
{
    BSP_AP6212_SDIO_OK = 0,
    BSP_AP6212_SDIO_ERR_PARAM = -1,
    BSP_AP6212_SDIO_ERR_CMD0 = -2,
    BSP_AP6212_SDIO_ERR_CMD5 = -3,
    BSP_AP6212_SDIO_ERR_NOT_READY = -4,
    BSP_AP6212_SDIO_ERR_CMD3 = -5,
    BSP_AP6212_SDIO_ERR_CMD7 = -6,
    BSP_AP6212_SDIO_ERR_CMD52 = -7,
    BSP_AP6212_SDIO_ERR_CMD53 = -8
} bsp_ap6212_sdio_status_t;

bsp_ap6212_sdio_status_t bsp_ap6212_sdio_probe(bsp_ap6212_sdio_probe_t *probe);
int bsp_ap6212_sdio_readb(uint8_t function, uint32_t address, uint8_t *value);
int bsp_ap6212_sdio_writeb(uint8_t function, uint32_t address, uint8_t value);
int bsp_ap6212_sdio_read_ext(uint8_t function,
                             uint32_t address,
                             uint8_t increment,
                             uint8_t *data,
                             uint32_t length,
                             uint32_t *last_sta);
int bsp_ap6212_sdio_write_ext(uint8_t function,
                              uint32_t address,
                              uint8_t increment,
                              const uint8_t *data,
                              uint32_t length,
                              uint32_t *last_sta);
int bsp_ap6212_sdio_ram_read(uint32_t address, uint8_t *data, uint32_t length);
int bsp_ap6212_sdio_ram_write(uint32_t address, const uint8_t *data, uint32_t length);
int bsp_ap6212_sdio_release_cm3(const bsp_ap6212_sdio_probe_t *probe);
int bsp_ap6212_sdio_enable_f2(uint8_t *io_enable, uint8_t *io_ready);
void bsp_ap6212_sdio_get_last_ext_debug(bsp_ap6212_sdio_ext_debug_t *debug);

#endif /* BSP_AP6212_SDIO_H */
