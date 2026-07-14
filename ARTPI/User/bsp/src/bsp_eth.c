#include "bsp.h"

#include <string.h>

#define BSP_ETH_PHY_ADDRESS_COUNT          32U
#define BSP_ETH_PHY_BASIC_CONTROL           0U
#define BSP_ETH_PHY_BASIC_STATUS            1U
#define BSP_ETH_PHY_ID1                     2U
#define BSP_ETH_PHY_ID2                     3U
#define BSP_ETH_PHY_STATUS                 31U
#define BSP_ETH_PHY_LINK_UP            0x0004U
#define BSP_ETH_PHY_AUTONEG_ENABLE      0x1000U
#define BSP_ETH_PHY_AUTONEG_RESTART     0x0200U
#define BSP_ETH_PHY_100_MBPS            0x0008U
#define BSP_ETH_PHY_FULL_DUPLEX         0x0010U

#define BSP_ETH_RX_BUFFER_SIZE             1536U
#define BSP_ETH_TRANSMIT_TIMEOUT_MS          100U

static ETH_HandleTypeDef eth_handle;
static ETH_TxPacketConfig eth_tx_config;
static uint8_t eth_mac_address[6] = {0x02U, 0x80U, 0xE1U, 0x75U, 0x00U, 0x01U};
static uint32_t eth_phy_address = BSP_ETH_PHY_ADDRESS_COUNT;
static uint8_t eth_initialized;
static uint8_t eth_started;
static bsp_eth_rx_callback_t eth_rx_callback;
static void *eth_rx_argument;
static bsp_eth_diagnostics_t eth_diagnostics;

__attribute__((section(".bss.eth_dma"), aligned(32)))
static ETH_DMADescTypeDef eth_rx_descriptors[ETH_RX_DESC_CNT];

__attribute__((section(".bss.eth_dma"), aligned(32)))
static ETH_DMADescTypeDef eth_tx_descriptors[ETH_TX_DESC_CNT];

__attribute__((section(".bss.eth_dma"), aligned(32)))
static uint8_t eth_rx_buffers[ETH_RX_DESC_CNT][BSP_ETH_RX_BUFFER_SIZE];

static HAL_StatusTypeDef bsp_eth_find_phy(void);
static void bsp_eth_reset_phy(void);

HAL_StatusTypeDef bsp_eth_init(void)
{
    uint32_t index;

    if (eth_initialized != 0U)
    {
        return HAL_OK;
    }

    memset(&eth_handle, 0, sizeof(eth_handle));
    memset(&eth_tx_config, 0, sizeof(eth_tx_config));
    memset(&eth_diagnostics, 0, sizeof(eth_diagnostics));

    bsp_eth_reset_phy();

    eth_handle.Instance = ETH;
    eth_handle.Init.MACAddr = eth_mac_address;
    eth_handle.Init.MediaInterface = HAL_ETH_RMII_MODE;
    eth_handle.Init.TxDesc = eth_tx_descriptors;
    eth_handle.Init.RxDesc = eth_rx_descriptors;
    eth_handle.Init.RxBuffLen = BSP_ETH_RX_BUFFER_SIZE;

    if (HAL_ETH_Init(&eth_handle) != HAL_OK)
    {
        return HAL_ERROR;
    }

    for (index = 0U; index < ETH_RX_DESC_CNT; index++)
    {
        if (HAL_ETH_DescAssignMemory(&eth_handle,
                                     index,
                                     eth_rx_buffers[index],
                                     NULL) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    HAL_ETH_SetMDIOClockRange(&eth_handle);
    if (bsp_eth_find_phy() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_ETH_WritePHYRegister(&eth_handle,
                                 eth_phy_address,
                                 BSP_ETH_PHY_BASIC_CONTROL,
                                 BSP_ETH_PHY_AUTONEG_ENABLE |
                                     BSP_ETH_PHY_AUTONEG_RESTART) != HAL_OK)
    {
        return HAL_ERROR;
    }

    eth_tx_config.Attributes = ETH_TX_PACKETS_FEATURES_CRCPAD;
    eth_tx_config.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
    eth_tx_config.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    eth_initialized = 1U;
    return HAL_OK;
}

HAL_StatusTypeDef bsp_eth_start(void)
{
    HAL_StatusTypeDef status;

    if (eth_initialized == 0U)
    {
        return HAL_ERROR;
    }
    if (eth_started != 0U)
    {
        return HAL_OK;
    }

    status = HAL_ETH_Start_IT(&eth_handle);
    if (status == HAL_OK)
    {
        eth_started = 1U;
    }
    return status;
}

HAL_StatusTypeDef bsp_eth_stop(void)
{
    HAL_StatusTypeDef status;

    if (eth_started == 0U)
    {
        return HAL_OK;
    }

    status = HAL_ETH_Stop_IT(&eth_handle);
    if (status == HAL_OK)
    {
        eth_started = 0U;
    }
    return status;
}

HAL_StatusTypeDef bsp_eth_transmit(const uint8_t *frame, uint32_t length)
{
    ETH_BufferTypeDef buffer = {0};
    HAL_StatusTypeDef status;

    if ((eth_started == 0U) || (frame == NULL) ||
        (length < 14U) || (length > BSP_ETH_FRAME_MAX_SIZE))
    {
        return HAL_ERROR;
    }

    buffer.buffer = (uint8_t *)frame;
    buffer.len = length;
    eth_tx_config.Length = length;
    eth_tx_config.TxBuffer = &buffer;

    status = HAL_ETH_Transmit(&eth_handle,
                              &eth_tx_config,
                              BSP_ETH_TRANSMIT_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        eth_diagnostics.transmitted_frames++;
    }
    else
    {
        eth_diagnostics.transmit_errors++;
    }
    return status;
}

HAL_StatusTypeDef bsp_eth_receive(uint8_t *frame,
                                  uint32_t capacity,
                                  uint32_t *length)
{
    ETH_BufferTypeDef buffers[ETH_RX_DESC_CNT] = {0};
    ETH_BufferTypeDef *buffer;
    uint32_t frame_length;
    uint32_t copied = 0U;
    uint32_t index;
    HAL_StatusTypeDef status;

    if ((eth_started == 0U) || (frame == NULL) || (length == NULL))
    {
        return HAL_ERROR;
    }

    for (index = 0U; index + 1U < ETH_RX_DESC_CNT; index++)
    {
        buffers[index].next = &buffers[index + 1U];
    }

    status = HAL_ETH_GetRxDataBuffer(&eth_handle, buffers);
    if (status != HAL_OK)
    {
        return status;
    }

    status = HAL_ETH_GetRxDataLength(&eth_handle, &frame_length);
    if ((status != HAL_OK) || (frame_length > capacity))
    {
        eth_diagnostics.receive_drops++;
        (void)HAL_ETH_BuildRxDescriptors(&eth_handle);
        return HAL_ERROR;
    }

    buffer = buffers;
    while ((buffer != NULL) && (copied < frame_length))
    {
        uint32_t bytes = buffer->len;

        if (bytes > (frame_length - copied))
        {
            bytes = frame_length - copied;
        }
        memcpy(&frame[copied], buffer->buffer, bytes);
        copied += bytes;
        buffer = buffer->next;
    }

    (void)HAL_ETH_BuildRxDescriptors(&eth_handle);
    if (copied != frame_length)
    {
        eth_diagnostics.receive_errors++;
        return HAL_ERROR;
    }

    *length = frame_length;
    eth_diagnostics.received_frames++;
    return HAL_OK;
}

HAL_StatusTypeDef bsp_eth_get_link(bsp_eth_link_t *link)
{
    uint32_t basic_status;
    uint32_t phy_status;

    if ((eth_initialized == 0U) || (link == NULL))
    {
        return HAL_ERROR;
    }

    if ((HAL_ETH_ReadPHYRegister(&eth_handle,
                                 eth_phy_address,
                                 BSP_ETH_PHY_BASIC_STATUS,
                                 &basic_status) != HAL_OK) ||
        (HAL_ETH_ReadPHYRegister(&eth_handle,
                                 eth_phy_address,
                                 BSP_ETH_PHY_BASIC_STATUS,
                                 &basic_status) != HAL_OK))
    {
        return HAL_ERROR;
    }

    link->link_up = ((basic_status & BSP_ETH_PHY_LINK_UP) != 0U) ? 1U : 0U;
    link->speed_mbps = 0U;
    link->full_duplex = 0U;
    if (link->link_up == 0U)
    {
        return HAL_OK;
    }

    if (HAL_ETH_ReadPHYRegister(&eth_handle,
                                eth_phy_address,
                                BSP_ETH_PHY_STATUS,
                                &phy_status) != HAL_OK)
    {
        return HAL_ERROR;
    }

    link->speed_mbps = ((phy_status & BSP_ETH_PHY_100_MBPS) != 0U) ? 100U : 10U;
    link->full_duplex = ((phy_status & BSP_ETH_PHY_FULL_DUPLEX) != 0U) ? 1U : 0U;
    return HAL_OK;
}

HAL_StatusTypeDef bsp_eth_apply_link(const bsp_eth_link_t *link)
{
    ETH_MACConfigTypeDef config = {0};

    if ((link == NULL) || (link->link_up == 0U))
    {
        return HAL_ERROR;
    }

    if (HAL_ETH_GetMACConfig(&eth_handle, &config) != HAL_OK)
    {
        return HAL_ERROR;
    }
    config.Speed = (link->speed_mbps == 100U) ? ETH_SPEED_100M : ETH_SPEED_10M;
    config.DuplexMode = (link->full_duplex != 0U) ?
                            ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    return HAL_ETH_SetMACConfig(&eth_handle, &config);
}

void bsp_eth_get_mac_address(uint8_t address[6])
{
    if (address != NULL)
    {
        memcpy(address, eth_mac_address, sizeof(eth_mac_address));
    }
}

uint32_t bsp_eth_get_phy_address(void)
{
    return eth_phy_address;
}

void bsp_eth_set_rx_callback(bsp_eth_rx_callback_t callback, void *argument)
{
    uint32_t interrupt_state = __get_PRIMASK();

    __disable_irq();
    eth_rx_callback = callback;
    eth_rx_argument = (callback != NULL) ? argument : NULL;
    if (interrupt_state == 0U)
    {
        __enable_irq();
    }
}

void bsp_eth_get_diagnostics(bsp_eth_diagnostics_t *diagnostics)
{
    if (diagnostics != NULL)
    {
        *diagnostics = eth_diagnostics;
    }
}

void bsp_eth_irq_handler(void)
{
    HAL_ETH_IRQHandler(&eth_handle);
}

void HAL_ETH_MspInit(ETH_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle->Instance != ETH)
    {
        return;
    }

    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF11_ETH;

    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &gpio);

    HAL_NVIC_SetPriority(ETH_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handle)
{
    if ((handle == &eth_handle) && (eth_rx_callback != NULL))
    {
        eth_rx_callback(eth_rx_argument);
    }
}

void HAL_ETH_DMAErrorCallback(ETH_HandleTypeDef *handle)
{
    if (handle == &eth_handle)
    {
        eth_diagnostics.dma_errors++;
    }
}

static HAL_StatusTypeDef bsp_eth_find_phy(void)
{
    uint32_t address;

    for (address = 0U; address < BSP_ETH_PHY_ADDRESS_COUNT; address++)
    {
        uint32_t id1;
        uint32_t id2;

        if ((HAL_ETH_ReadPHYRegister(&eth_handle,
                                     address,
                                     BSP_ETH_PHY_ID1,
                                     &id1) == HAL_OK) &&
            (HAL_ETH_ReadPHYRegister(&eth_handle,
                                     address,
                                     BSP_ETH_PHY_ID2,
                                     &id2) == HAL_OK) &&
            (id1 != 0U) && (id1 != 0xFFFFU) &&
            (id2 != 0U) && (id2 != 0xFFFFU))
        {
            eth_phy_address = address;
            return HAL_OK;
        }
    }
    return HAL_ERROR;
}

static void bsp_eth_reset_phy(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
    bsp_delay_ms(50U);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET);
    bsp_delay_ms(50U);
}
