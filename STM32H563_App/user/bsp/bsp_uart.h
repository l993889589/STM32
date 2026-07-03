#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#include "main.h"


/* Logical UART roles. Physical HAL handles are assigned in bsp_board.c. */
typedef enum
{
    BSP_UART_W800_AT = 0,
    BSP_UART_RS485_1,
    BSP_UART_RS485_2,
    BSP_UART_DEBUG,
    BSP_UART_COUNT
} bsp_uart_port_t;

#define BSP_UART_RS485 BSP_UART_RS485_1



typedef void (*bsp_uart_rx_cb_t)(bsp_uart_port_t port, const uint8_t *data, uint16_t len, void *arg);
typedef void (*bsp_uart_tx_cb_t)(bsp_uart_port_t port, void *arg);



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




bsp_uart_desc_t *bsp_uart_get_desc(bsp_uart_port_t port);
int bsp_uart_bind(bsp_uart_port_t port, UART_HandleTypeDef *handle, uint8_t use_dma, uint8_t cache_invalidate);
uint8_t bsp_uart_rx_uses_dma(bsp_uart_port_t port);
void bsp_uart_init(void);
int bsp_uart_register_rx_callback(bsp_uart_port_t port, bsp_uart_rx_cb_t cb, void *arg);
int bsp_uart_start_rx(bsp_uart_port_t port, uint8_t *buf, uint16_t len);
int bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, uint16_t len, uint32_t timeout_ms);
int bsp_uart_write_wait_complete(bsp_uart_port_t port, const uint8_t *data, uint16_t len, uint32_t timeout_ms);
UART_HandleTypeDef *bsp_uart_handle(bsp_uart_port_t port);
uint32_t bsp_uart_rx_events(bsp_uart_port_t port);

int bsp_uart_start_rx_byte(bsp_uart_port_t port);
#endif /* BSP_UART_H */
