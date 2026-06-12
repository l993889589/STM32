#include "modbus_slave.h"

#include <stdio.h>

static void modbus_slave_print_frame(const uint8_t *adu, uint16_t len)
{
    printf(" frame:");
    for(uint16_t i = 0U; i < len; i++)
        printf(" %02X", adu[i]);
    printf("\r\n");
}

static int modbus_slave_send_exception(modbus_slave_t *slave, uint8_t unit, uint8_t function, uint8_t code)
{
    uint8_t rsp[5];
    uint16_t len;

    if(unit == MODBUS_RTU_BROADCAST_ID)
        return 0;

    rsp[0] = unit;
    rsp[1] = (uint8_t)(function | 0x80U);
    rsp[2] = code;
    len = modbus_rtu_append_crc(rsp, 3U, sizeof(rsp));

    slave->exception_count++;

    if(slave->tx && len != 0U)
    {
        slave->tx_frames++;
        (void)slave->tx(rsp, len, slave->tx_arg);
        return 1;
    }

    return 1;
}

static int modbus_slave_read_holding(modbus_slave_t *slave, const uint8_t *adu, uint16_t len)
{
    uint16_t start;
    uint16_t count;
    uint8_t rsp[MODBUS_RTU_MAX_ADU];
    uint16_t rsp_len;

    if(len != 8U)
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_VALUE);

    start = modbus_get_u16(&adu[2]);
    count = modbus_get_u16(&adu[4]);

    /* Address is zero-based: 0x0000 maps to holding register 40001 in many tools. */
    if(count == 0U || count > 125U || start >= slave->holding_count || count > (uint16_t)(slave->holding_count - start))
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_ADDRESS);

    rsp[0] = adu[0];
    rsp[1] = adu[1];
    rsp[2] = (uint8_t)(count * 2U);

    for(uint16_t i = 0U; i < count; i++)
        modbus_put_u16(&rsp[3U + (uint16_t)(i * 2U)], slave->holding_regs[start + i]);

    rsp_len = modbus_rtu_append_crc(rsp, (uint16_t)(3U + count * 2U), sizeof(rsp));

    if(slave->tx && rsp_len != 0U)
    {
        slave->tx_frames++;
        (void)slave->tx(rsp, rsp_len, slave->tx_arg);
        return 1;
    }

    return 1;
}

static int modbus_slave_write_single(modbus_slave_t *slave, const uint8_t *adu, uint16_t len)
{
    uint16_t addr;
    uint16_t value;

    if(len != 8U)
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_VALUE);

    addr = modbus_get_u16(&adu[2]);
    value = modbus_get_u16(&adu[4]);

    if(addr >= slave->holding_count)
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_ADDRESS);

    slave->holding_regs[addr] = value;

    if(adu[0] == MODBUS_RTU_BROADCAST_ID)
        return 0;

    if(slave->tx)
    {
        slave->tx_frames++;
        (void)slave->tx(adu, len, slave->tx_arg);
        return 1;
    }

    return 1;
}

static int modbus_slave_write_multiple(modbus_slave_t *slave, const uint8_t *adu, uint16_t len)
{
    uint16_t start;
    uint16_t count;
    uint8_t byte_count;
    uint8_t rsp[8];
    uint16_t rsp_len;

    if(len < 11U)
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_VALUE);

    start = modbus_get_u16(&adu[2]);
    count = modbus_get_u16(&adu[4]);
    byte_count = adu[6];

    if(count == 0U || count > 123U || byte_count != (uint8_t)(count * 2U) || len != (uint16_t)(9U + byte_count))
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_VALUE);

    if(start >= slave->holding_count || count > (uint16_t)(slave->holding_count - start))
        return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_ADDRESS);

    for(uint16_t i = 0U; i < count; i++)
        slave->holding_regs[start + i] = modbus_get_u16(&adu[7U + (uint16_t)(i * 2U)]);

    if(adu[0] == MODBUS_RTU_BROADCAST_ID)
        return 0;

    rsp[0] = adu[0];
    rsp[1] = adu[1];
    rsp[2] = adu[2];
    rsp[3] = adu[3];
    rsp[4] = adu[4];
    rsp[5] = adu[5];
    rsp_len = modbus_rtu_append_crc(rsp, 6U, sizeof(rsp));

    if(slave->tx && rsp_len != 0U)
    {
        slave->tx_frames++;
        (void)slave->tx(rsp, rsp_len, slave->tx_arg);
        return 1;
    }

    return 1;
}

void modbus_slave_init(modbus_slave_t *slave,
                       uint8_t unit_id,
                       uint16_t *holding_regs,
                       uint16_t holding_count,
                       modbus_tx_cb_t tx,
                       void *tx_arg)
{
    if(!slave)
        return;

    slave->unit_id = unit_id;
    slave->holding_regs = holding_regs;
    slave->holding_count = holding_count;
    slave->tx = tx;
    slave->tx_arg = tx_arg;
    slave->rx_frames = 0U;
    slave->tx_frames = 0U;
    slave->crc_errors = 0U;
    slave->exception_count = 0U;
}

int modbus_slave_process(modbus_slave_t *slave, const uint8_t *adu, uint16_t len)
{
    if(!slave || !adu || len < 4U)
        return 0;

    /*
     * Return convention:
     *   0: this frame is not handled by this slave, dispatcher may try another protocol.
     *   1: this frame is handled, even if it produced a Modbus exception response.
     */
    if(!modbus_rtu_check_crc(adu, len))
    {
        uint16_t calc = modbus_crc16(adu, (uint16_t)(len - 2U));
        uint16_t got = (uint16_t)(((uint16_t)adu[len - 1U] << 8) | adu[len - 2U]);
        slave->crc_errors++;
        printf("modbus slave ignore: crc error len=%u calc=%04X got=%04X\r\n",
               len, calc, got);
        modbus_slave_print_frame(adu, len);
        return 0;
    }

    if(adu[0] != slave->unit_id && adu[0] != MODBUS_RTU_BROADCAST_ID)
    {
        printf("modbus slave ignore: unit=%u local=%u\r\n", adu[0], slave->unit_id);
        return 0;
    }

    slave->rx_frames++;

    switch(adu[1])
    {
    case MODBUS_FC_READ_HOLDING:
        if(adu[0] == MODBUS_RTU_BROADCAST_ID)
            return 1;
        return modbus_slave_read_holding(slave, adu, len);

    case MODBUS_FC_WRITE_SINGLE_REG:
        return modbus_slave_write_single(slave, adu, len);

    case MODBUS_FC_WRITE_MULTIPLE_REGS:
        return modbus_slave_write_multiple(slave, adu, len);

    default:
        if(adu[0] != MODBUS_RTU_BROADCAST_ID)
            return modbus_slave_send_exception(slave, adu[0], adu[1], MODBUS_EX_ILLEGAL_FUNCTION);
        return 1;
    }
}

ldc_proto_result_t modbus_slave_dispatch(const uint8_t *adu, uint32_t len, void *arg)
{
    modbus_slave_t *slave = (modbus_slave_t *)arg;

    if(!slave || len > 0xFFFFU)
        return LDC_PROTO_UNHANDLED;

    return modbus_slave_process(slave, adu, (uint16_t)len) ? LDC_PROTO_HANDLED : LDC_PROTO_UNHANDLED;
}
