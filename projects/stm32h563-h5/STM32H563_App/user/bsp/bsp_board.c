#include "bsp_board.h"

#include <stdint.h>

#include "gd25lq128.h"
#include "spi.h"
#include "usart.h"

typedef struct
{
    bsp_uart_port_t port;
    UART_HandleTypeDef *handle;
    uint8_t use_dma;
    uint8_t cache_invalidate;
} bsp_board_uart_binding_t;

/* This table is the only physical UART binding for BSP_UART_* roles. */
static const bsp_board_uart_binding_t g_board_uart_bindings[] =
{
    {BSP_UART_W800_AT, &huart1, 1U, 1U},
    {BSP_UART_RS485_1, &huart2, 0U, 0U},
    {BSP_UART_RS485_2, &huart4, 0U, 0U},
    {BSP_UART_DEBUG,   &huart3, 0U, 0U},
};

int bsp_board_init(void)
{
    for(uint32_t i = 0U; i < (sizeof(g_board_uart_bindings) / sizeof(g_board_uart_bindings[0])); i++)
    {
        const bsp_board_uart_binding_t *binding = &g_board_uart_bindings[i];
        if(bsp_uart_bind(binding->port,
                         binding->handle,
                         binding->use_dma,
                         binding->cache_invalidate) != 0)
        {
            return -1;
        }
    }

    (void)gd25lq128_bind(&hspi1);
    return 0;
}
