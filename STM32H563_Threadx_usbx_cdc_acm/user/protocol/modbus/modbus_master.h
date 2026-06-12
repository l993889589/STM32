#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <stdint.h>
#include "modbus_rtu.h"
#include "ldc_proto_dispatcher.h"

typedef enum
{
    MODBUS_MASTER_RSP_NONE = 0,
    MODBUS_MASTER_RSP_READ_HOLDING,
    MODBUS_MASTER_RSP_WRITE_SINGLE,
    MODBUS_MASTER_RSP_WRITE_MULTIPLE,
    MODBUS_MASTER_RSP_EXCEPTION
} modbus_master_rsp_type_t;

typedef struct
{
    modbus_master_rsp_type_t type;
    uint8_t unit_id;
    uint8_t function;
    uint8_t exception_code;
    uint16_t address;
    uint16_t quantity;
    uint16_t *registers;
    uint16_t register_capacity;
    uint16_t register_count;
} modbus_master_response_t;

typedef struct
{
    modbus_tx_cb_t tx;
    void *tx_arg;
    uint8_t last_unit_id;      /* Last request target, used to match responses. */
    uint8_t last_function;     /* Last request function code. */
    uint16_t last_address;     /* Last request start/register address. */
    uint16_t last_quantity;    /* Last request register count. */
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t crc_errors;
    uint32_t exception_count;
} modbus_master_t;

void modbus_master_init(modbus_master_t *master, modbus_tx_cb_t tx, void *tx_arg);
int modbus_master_read_holding(modbus_master_t *master, uint8_t unit_id, uint16_t start, uint16_t count);
int modbus_master_write_single_reg(modbus_master_t *master, uint8_t unit_id, uint16_t address, uint16_t value);
int modbus_master_write_multiple_regs(modbus_master_t *master, uint8_t unit_id, uint16_t start,
                                      const uint16_t *values, uint16_t count);
int modbus_master_parse_response(modbus_master_t *master, const uint8_t *adu, uint16_t len,
                                  modbus_master_response_t *response);
ldc_proto_result_t modbus_master_dispatch(const uint8_t *adu, uint32_t len, void *arg);

#endif
