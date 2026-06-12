#include "modbus_master.h"

static int modbus_master_send(modbus_master_t *master, uint8_t *adu, uint16_t pdu_len)
{
    uint16_t len;

    if(!master || !master->tx)
        return -1;

    len = modbus_rtu_append_crc(adu, pdu_len, MODBUS_RTU_MAX_ADU);
    if(len == 0U)
        return -2;

    /* Save request context so parse_response can validate the next response. */
    master->last_unit_id = adu[0];
    master->last_function = adu[1];
    master->tx_frames++;

    return master->tx(adu, len, master->tx_arg);
}

void modbus_master_init(modbus_master_t *master, modbus_tx_cb_t tx, void *tx_arg)
{
    if(!master)
        return;

    master->tx = tx;
    master->tx_arg = tx_arg;
    master->last_unit_id = 0U;
    master->last_function = 0U;
    master->last_address = 0U;
    master->last_quantity = 0U;
    master->tx_frames = 0U;
    master->rx_frames = 0U;
    master->crc_errors = 0U;
    master->exception_count = 0U;
}

int modbus_master_read_holding(modbus_master_t *master, uint8_t unit_id, uint16_t start, uint16_t count)
{
    uint8_t req[8];

    if(!master || count == 0U || count > 125U)
        return -1;

    req[0] = unit_id;
    req[1] = MODBUS_FC_READ_HOLDING;
    modbus_put_u16(&req[2], start);
    modbus_put_u16(&req[4], count);

    master->last_address = start;
    master->last_quantity = count;

    return modbus_master_send(master, req, 6U);
}

int modbus_master_write_single_reg(modbus_master_t *master, uint8_t unit_id, uint16_t address, uint16_t value)
{
    uint8_t req[8];

    if(!master)
        return -1;

    req[0] = unit_id;
    req[1] = MODBUS_FC_WRITE_SINGLE_REG;
    modbus_put_u16(&req[2], address);
    modbus_put_u16(&req[4], value);

    master->last_address = address;
    master->last_quantity = 1U;

    return modbus_master_send(master, req, 6U);
}

int modbus_master_write_multiple_regs(modbus_master_t *master, uint8_t unit_id, uint16_t start,
                                      const uint16_t *values, uint16_t count)
{
    uint8_t req[MODBUS_RTU_MAX_ADU];
    uint16_t pdu_len;

    if(!master || !values || count == 0U || count > 123U)
        return -1;

    pdu_len = (uint16_t)(7U + count * 2U);
    if((uint16_t)(pdu_len + 2U) > MODBUS_RTU_MAX_ADU)
        return -2;

    req[0] = unit_id;
    req[1] = MODBUS_FC_WRITE_MULTIPLE_REGS;
    modbus_put_u16(&req[2], start);
    modbus_put_u16(&req[4], count);
    req[6] = (uint8_t)(count * 2U);

    for(uint16_t i = 0U; i < count; i++)
        modbus_put_u16(&req[7U + (uint16_t)(i * 2U)], values[i]);

    master->last_address = start;
    master->last_quantity = count;

    return modbus_master_send(master, req, pdu_len);
}

int modbus_master_parse_response(modbus_master_t *master, const uint8_t *adu, uint16_t len,
                                  modbus_master_response_t *response)
{
    uint8_t function;

    if(!master || !adu || !response || len < 4U)
        return -1;

    response->type = MODBUS_MASTER_RSP_NONE;
    response->unit_id = adu[0];
    response->function = adu[1];
    response->exception_code = 0U;
    response->address = master->last_address;
    response->quantity = master->last_quantity;
    response->register_count = 0U;

    if(!modbus_rtu_check_crc(adu, len))
    {
        master->crc_errors++;
        return -2;
    }

    if(adu[0] != master->last_unit_id)
        return 0;

    function = adu[1];

    /* Exception response echoes function | 0x80 and carries one exception code. */
    if((function & 0x80U) != 0U)
    {
        if(len != 5U)
            return -3;

        master->exception_count++;
        master->rx_frames++;
        response->type = MODBUS_MASTER_RSP_EXCEPTION;
        response->function = (uint8_t)(function & 0x7FU);
        response->exception_code = adu[2];
        return 1;
    }

    if(function != master->last_function)
        return 0;

    switch(function)
    {
    case MODBUS_FC_READ_HOLDING:
    {
        uint8_t byte_count;
        uint16_t reg_count;

        if(len < 5U)
            return -3;

        byte_count = adu[2];
        if((byte_count & 1U) != 0U || len != (uint16_t)(5U + byte_count))
            return -3;

        reg_count = (uint16_t)(byte_count / 2U);
        if(reg_count > response->register_capacity)
            return -4;

        /* Caller provides the output register buffer to avoid heap allocation. */
        for(uint16_t i = 0U; i < reg_count; i++)
            response->registers[i] = modbus_get_u16(&adu[3U + (uint16_t)(i * 2U)]);

        response->type = MODBUS_MASTER_RSP_READ_HOLDING;
        response->register_count = reg_count;
        master->rx_frames++;
        return 1;
    }

    case MODBUS_FC_WRITE_SINGLE_REG:
        if(len != 8U)
            return -3;
        response->type = MODBUS_MASTER_RSP_WRITE_SINGLE;
        response->address = modbus_get_u16(&adu[2]);
        response->quantity = 1U;
        master->rx_frames++;
        return 1;

    case MODBUS_FC_WRITE_MULTIPLE_REGS:
        if(len != 8U)
            return -3;
        response->type = MODBUS_MASTER_RSP_WRITE_MULTIPLE;
        response->address = modbus_get_u16(&adu[2]);
        response->quantity = modbus_get_u16(&adu[4]);
        master->rx_frames++;
        return 1;

    default:
        return 0;
    }
}

ldc_proto_result_t modbus_master_dispatch(const uint8_t *adu, uint32_t len, void *arg)
{
    modbus_master_t *master = (modbus_master_t *)arg;
    modbus_master_response_t rsp;
    uint16_t regs[125];
    int ret;

    if(!master || len > 0xFFFFU)
        return LDC_PROTO_UNHANDLED;

    rsp.registers = regs;
    rsp.register_capacity = 125U;
    ret = modbus_master_parse_response(master, adu, (uint16_t)len, &rsp);

    return (ret > 0) ? LDC_PROTO_HANDLED : LDC_PROTO_UNHANDLED;
}
