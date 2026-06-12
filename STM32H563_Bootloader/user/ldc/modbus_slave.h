#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#include <stdint.h>
#include "modbus_rtu.h"
#include "ldc_proto_dispatcher.h"

typedef struct
{
    uint8_t unit_id;
    uint16_t *holding_regs;
    uint16_t holding_count;
    modbus_tx_cb_t tx;
    void *tx_arg;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t crc_errors;
    uint32_t exception_count;
} modbus_slave_t;

void modbus_slave_init(modbus_slave_t *slave,
                       uint8_t unit_id,
                       uint16_t *holding_regs,
                       uint16_t holding_count,
                       modbus_tx_cb_t tx,
                       void *tx_arg);
ldc_proto_result_t modbus_slave_dispatch(const uint8_t *adu, uint32_t len, void *arg);
int modbus_slave_process(modbus_slave_t *slave, const uint8_t *adu, uint16_t len);

#endif
