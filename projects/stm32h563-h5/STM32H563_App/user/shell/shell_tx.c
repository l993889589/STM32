
#include "shell_tx.h"
#include <string.h>

#define TX_SIZE 512

static uint8_t tx_buf[TX_SIZE];
static volatile uint16_t w = 0;
static volatile uint16_t r = 0;

void shell_tx_init(void)
{
    w = r = 0;
}

void shell_tx_write(const uint8_t *data, uint16_t len)
{
    for(uint16_t i=0;i<len;i++)
    {
        tx_buf[w] = data[i];
        w = (w + 1) % TX_SIZE;
    }
}

/* DMA trigger stub (HAL/LL adapt here) */
void shell_tx_dma_kick(void)
{
    if(r == w) return;

    uint16_t len = (w > r) ? (w - r) : (TX_SIZE - r);

    /* HAL_UART_Transmit_DMA(...) placeholder */
    r = (r + len) % TX_SIZE;
}
