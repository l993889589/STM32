#ifndef BSP_ETH_H
#define BSP_ETH_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

#define BSP_ETH_FRAME_MAX_SIZE  1536U

typedef struct
{
    uint8_t link_up;
    uint8_t full_duplex;
    uint16_t speed_mbps;
} bsp_eth_link_t;

typedef struct
{
    uint32_t received_frames;
    uint32_t transmitted_frames;
    uint32_t receive_drops;
    uint32_t receive_errors;
    uint32_t transmit_errors;
    uint32_t dma_errors;
    uint32_t transmit_timeouts;
    uint32_t transmit_recoveries;
    uint32_t last_hal_error;
    uint32_t last_dma_status;
} bsp_eth_diagnostics_t;

typedef void (*bsp_eth_rx_callback_t)(void *argument);

HAL_StatusTypeDef bsp_eth_init(void);
HAL_StatusTypeDef bsp_eth_start(void);
HAL_StatusTypeDef bsp_eth_stop(void);
HAL_StatusTypeDef bsp_eth_transmit(const uint8_t *frame, uint32_t length);
HAL_StatusTypeDef bsp_eth_receive(uint8_t *frame,
                                  uint32_t capacity,
                                  uint32_t *length);
HAL_StatusTypeDef bsp_eth_get_link(bsp_eth_link_t *link);
HAL_StatusTypeDef bsp_eth_apply_link(const bsp_eth_link_t *link);
void bsp_eth_get_mac_address(uint8_t address[6]);
uint32_t bsp_eth_get_phy_address(void);
void bsp_eth_set_rx_callback(bsp_eth_rx_callback_t callback, void *argument);
void bsp_eth_get_diagnostics(bsp_eth_diagnostics_t *diagnostics);
void bsp_eth_irq_handler(void);

#endif
