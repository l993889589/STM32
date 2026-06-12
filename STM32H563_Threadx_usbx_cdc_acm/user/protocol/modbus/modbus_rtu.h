#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stdint.h>
#include <stdbool.h>

#define MODBUS_RTU_MAX_ADU             256U
#define MODBUS_RTU_BROADCAST_ID        0U

#define MODBUS_FC_READ_HOLDING         0x03U
#define MODBUS_FC_WRITE_SINGLE_REG     0x06U
#define MODBUS_FC_WRITE_MULTIPLE_REGS  0x10U

#define MODBUS_EX_ILLEGAL_FUNCTION     0x01U
#define MODBUS_EX_ILLEGAL_ADDRESS      0x02U
#define MODBUS_EX_ILLEGAL_VALUE        0x03U
#define MODBUS_EX_SERVER_FAILURE       0x04U

typedef int (*modbus_tx_cb_t)(const uint8_t *data, uint16_t len, void *arg);

/* Standard Modbus RTU CRC16. The wire order is low byte first, high byte second. */
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);
bool modbus_rtu_check_crc(const uint8_t *adu, uint16_t len);
uint16_t modbus_rtu_append_crc(uint8_t *adu, uint16_t len, uint16_t max_len);

/* Modbus multi-byte fields are big-endian inside the PDU. */
static inline uint16_t modbus_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline void modbus_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

#endif
