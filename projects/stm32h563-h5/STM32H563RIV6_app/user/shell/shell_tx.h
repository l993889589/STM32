
#pragma once
#include <stdint.h>

void shell_tx_init(void);
void shell_tx_write(const uint8_t *data, uint16_t len);
void shell_tx_dma_kick(void);
