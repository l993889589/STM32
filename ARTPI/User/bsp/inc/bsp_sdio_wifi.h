#ifndef BSP_SDIO_WIFI_H
#define BSP_SDIO_WIFI_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

typedef struct
{
    uint32_t ocr;
    uint16_t relative_card_address;
    uint8_t cccr_revision;
    uint8_t sd_revision;
    uint8_t io_enable;
    uint32_t last_status;
} bsp_sdio_wifi_probe_result_t;

typedef void (*bsp_sdio_wifi_oob_callback_t)(void);

HAL_StatusTypeDef bsp_sdio_wifi_init(void);
HAL_StatusTypeDef bsp_sdio_wifi_probe(bsp_sdio_wifi_probe_result_t *result);
HAL_StatusTypeDef bsp_sdio_wifi_transfer(uint8_t write,
                                         uint8_t function,
                                         uint32_t address,
                                         uint8_t *data,
                                         uint16_t data_size,
                                         uint16_t function_block_size);
HAL_StatusTypeDef bsp_sdio_wifi_set_high_speed(void);
void bsp_sdio_wifi_set_power(uint8_t enabled);
void bsp_sdio_wifi_set_oob_callback(bsp_sdio_wifi_oob_callback_t callback);
void bsp_sdio_wifi_enable_oob_interrupt(uint8_t enabled);
void bsp_sdio_wifi_oob_irq_handler(void);
uint32_t bsp_sdio_wifi_get_oob_interrupt_count(void);
uint8_t bsp_sdio_wifi_get_oob_level(void);
uint32_t bsp_sdio_wifi_get_clock(void);

#endif
