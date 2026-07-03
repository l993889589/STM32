#include "bsp_uart.h"

#include <stddef.h>

typedef struct
{
    UART_HandleTypeDef *handle;
    uint8_t use_dma;
    uint8_t cache_invalidate;
    uint8_t rx_byte_mode;
    uint8_t *rx_buf;
    uint16_t rx_size;
    uint8_t rx_byte;
    bsp_uart_rx_cb_t rx_cb;
    void *rx_arg;
    bsp_uart_tx_cb_t tx_cb;
    void *tx_arg;
    uint32_t rx_events;
} bsp_uart_desc_t;

static bsp_uart_desc_t g_uart_desc[BSP_UART_COUNT] =
{
    [BSP_UART4] = {0},
    [BSP_USART3] = {0},
};

static bsp_uart_desc_t *bsp_uart_get_desc(bsp_uart_port_t port)
{
    if(port >= BSP_UART_COUNT)
        return NULL;

    return &g_uart_desc[port];
}

static bsp_uart_desc_t *bsp_uart_find_desc(UART_HandleTypeDef *huart, bsp_uart_port_t *port)
{
    for(uint32_t i = 0U; i < BSP_UART_COUNT; i++)
    {
        if(g_uart_desc[i].handle == huart)
        {
            if(port)
                *port = (bsp_uart_port_t)i;

            return &g_uart_desc[i];
        }
    }

    return NULL;
}

static int bsp_uart_restart_rx(bsp_uart_desc_t *desc)
{
    HAL_StatusTypeDef status;

    if(!desc || !desc->handle)
        return -1;

    if(desc->rx_byte_mode != 0U)
    {
        status = HAL_UART_Receive_IT(desc->handle, &desc->rx_byte, 1U);
        return (status == HAL_OK) ? 0 : -1;
    }

    if(!desc->rx_buf || desc->rx_size == 0U)
        return -1;

    if(desc->use_dma)
    {
        status = HAL_UARTEx_ReceiveToIdle_DMA(desc->handle, desc->rx_buf, desc->rx_size);
        if(status == HAL_OK && desc->handle->hdmarx)
            __HAL_DMA_DISABLE_IT(desc->handle->hdmarx, DMA_IT_HT);
    }
    else
    {
        status = HAL_UARTEx_ReceiveToIdle_IT(desc->handle, desc->rx_buf, desc->rx_size);
    }

    return (status == HAL_OK) ? 0 : -1;
}

int bsp_uart_bind(bsp_uart_port_t port, UART_HandleTypeDef *handle, uint8_t use_dma, uint8_t cache_invalidate)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc || !handle)
        return -1;

    desc->handle = handle;
    desc->use_dma = use_dma ? 1U : 0U;
    desc->cache_invalidate = cache_invalidate ? 1U : 0U;
    return 0;
}

void bsp_uart_init(void)
{
    for(uint32_t i = 0U; i < BSP_UART_COUNT; i++)
    {
        g_uart_desc[i].rx_buf = NULL;
        g_uart_desc[i].rx_size = 0U;
        g_uart_desc[i].rx_byte = 0U;
        g_uart_desc[i].rx_byte_mode = 0U;
        g_uart_desc[i].rx_cb = NULL;
        g_uart_desc[i].rx_arg = NULL;
        g_uart_desc[i].tx_cb = NULL;
        g_uart_desc[i].tx_arg = NULL;
        g_uart_desc[i].rx_events = 0U;
    }
}

uint32_t bsp_uart_rx_events(bsp_uart_port_t port)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    return desc ? desc->rx_events : 0U;
}

int bsp_uart_register_rx_callback(bsp_uart_port_t port, bsp_uart_rx_cb_t cb, void *arg)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc)
        return -1;

    desc->rx_cb = cb;
    desc->rx_arg = arg;
    return 0;
}

int bsp_uart_register_tx_callback(bsp_uart_port_t port, bsp_uart_tx_cb_t cb, void *arg)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc)
        return -1;

    desc->tx_cb = cb;
    desc->tx_arg = arg;
    return 0;
}

int bsp_uart_start_rx(bsp_uart_port_t port, uint8_t *buf, uint16_t len)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc || !buf || len == 0U)
        return -1;

    desc->rx_buf = buf;
    desc->rx_size = len;
    desc->rx_byte_mode = 0U;
    return bsp_uart_restart_rx(desc);
}

int bsp_uart_start_rx_byte(bsp_uart_port_t port)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc)
        return -1;

    desc->rx_buf = &desc->rx_byte;
    desc->rx_size = 1U;
    desc->rx_byte_mode = 1U;
    return bsp_uart_restart_rx(desc);
}

int bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc || !desc->handle || !data || len == 0U)
        return -1;

    return (HAL_UART_Transmit(desc->handle, (uint8_t *)data, len, timeout_ms) == HAL_OK) ? (int)len : -1;
}

int bsp_uart_write_wait_complete(bsp_uart_port_t port, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);
    uint32_t start;

    if(bsp_uart_write(port, data, len, timeout_ms) < 0)
        return -1;

    start = HAL_GetTick();
    while(__HAL_UART_GET_FLAG(desc->handle, UART_FLAG_TC) == RESET)
    {
        if(timeout_ms != HAL_MAX_DELAY && (HAL_GetTick() - start) > timeout_ms)
            return -1;
    }

    return (int)len;
}

int bsp_uart_write_it(bsp_uart_port_t port, const uint8_t *data, uint16_t len)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    if(!desc || !desc->handle || !data || len == 0U)
        return -1;

    return (HAL_UART_Transmit_IT(desc->handle, (uint8_t *)data, len) == HAL_OK) ? (int)len : 0;
}

UART_HandleTypeDef *bsp_uart_handle(bsp_uart_port_t port)
{
    bsp_uart_desc_t *desc = bsp_uart_get_desc(port);

    return desc ? desc->handle : NULL;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    bsp_uart_port_t port;
    bsp_uart_desc_t *desc = bsp_uart_find_desc(huart, &port);

    if(!desc)
        return;

    if(desc->rx_byte_mode != 0U)
        return;

    if(size > desc->rx_size)
        size = desc->rx_size;

    desc->rx_events++;

    if(size != 0U && desc->rx_buf)
    {
        if(desc->cache_invalidate)
        {
            /* DCache invalidate hook for cacheable DMA buffers. */
        }

        if(desc->rx_cb)
            desc->rx_cb(port, desc->rx_buf, size, desc->rx_arg);
    }

    (void)bsp_uart_restart_rx(desc);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    bsp_uart_port_t port;
    bsp_uart_desc_t *desc = bsp_uart_find_desc(huart, &port);

    if(!desc || desc->rx_byte_mode == 0U)
        return;

    desc->rx_events++;
    if(desc->rx_cb)
        desc->rx_cb(port, &desc->rx_byte, 1U, desc->rx_arg);

    (void)bsp_uart_restart_rx(desc);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    bsp_uart_desc_t *desc = bsp_uart_find_desc(huart, NULL);

    if(desc)
        (void)bsp_uart_restart_rx(desc);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    bsp_uart_port_t port;
    bsp_uart_desc_t *desc = bsp_uart_find_desc(huart, &port);

    if(desc && desc->tx_cb)
        desc->tx_cb(port, desc->tx_arg);
}

int stdout_putchar(int ch)
{
    UART4->TDR = (uint8_t)ch;
    while((UART4->ISR & USART_ISR_TC) == 0U)
    {
    }
    return ch;
}
