/*
 * STM32 Modbus RTU slave core.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "modbus.h"

#include <stddef.h>
#include <string.h>

static uint16_t modbus_get_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void modbus_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

uint16_t modbus_rtu_crc(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t pos;

    if(data == NULL)
        return 0U;

    for(pos = 0U; pos < length; pos++)
    {
        uint8_t bit;
        crc ^= data[pos];
        for(bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
    }

    return crc;
}

bool modbus_rtu_crc_valid(const uint8_t *adu, uint16_t length)
{
    uint16_t received;

    if(adu == NULL || length < 4U)
        return false;

    received = (uint16_t)(((uint16_t)adu[length - 1U] << 8) | adu[length - 2U]);
    return received == modbus_rtu_crc(adu, (uint16_t)(length - 2U));
}

static uint16_t modbus_append_crc(uint8_t *adu, uint16_t length)
{
    uint16_t crc = modbus_rtu_crc(adu, length);
    adu[length++] = (uint8_t)crc;
    adu[length++] = (uint8_t)(crc >> 8);
    return length;
}

static bool modbus_range_offset(uint16_t address, uint16_t quantity,
                                uint16_t start, uint16_t count, uint16_t *offset)
{
    uint32_t end = (uint32_t)address + quantity;
    uint32_t map_end = (uint32_t)start + count;

    if(quantity == 0U || address < start || end > map_end)
        return false;

    *offset = (uint16_t)(address - start);
    return true;
}

static modbus_process_result_t modbus_send(modbus_t *ctx, uint16_t length)
{
    int sent;

    length = modbus_append_crc(ctx->response, length);
    sent = ctx->send(ctx->response, length, ctx->send_arg);
    if(sent != (int)length)
    {
        ctx->last_error = MODBUS_ERROR_TRANSPORT;
        ctx->stats.transport_errors++;
        return MODBUS_PROCESS_ERROR;
    }

    ctx->stats.tx_frames++;
    return MODBUS_PROCESS_REPLIED;
}

static modbus_process_result_t modbus_exception(modbus_t *ctx, uint8_t function,
                                                 modbus_exception_t exception,
                                                 bool broadcast)
{
    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;

    ctx->response[0] = ctx->slave;
    ctx->response[1] = (uint8_t)(function | 0x80U);
    ctx->response[2] = (uint8_t)exception;
    ctx->stats.exceptions++;
    return modbus_send(ctx, 3U);
}

static modbus_process_result_t modbus_read_bits(modbus_t *ctx, const uint8_t *adu,
                                                 bool inputs, bool broadcast)
{
    modbus_mapping_t *map = ctx->mapping;
    uint16_t address;
    uint16_t quantity;
    uint16_t offset;
    uint16_t count;
    uint16_t start;
    uint8_t *table;
    uint8_t byte_count;
    uint16_t i;

    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;

    address = modbus_get_u16(&adu[2]);
    quantity = modbus_get_u16(&adu[4]);
    table = inputs ? map->tab_input_bits : map->tab_bits;
    start = inputs ? map->start_input_bits : map->start_bits;
    count = inputs ? map->nb_input_bits : map->nb_bits;

    if(quantity == 0U || quantity > MODBUS_MAX_READ_BITS)
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, false);
    if(table == NULL || !modbus_range_offset(address, quantity, start, count, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, false);

    byte_count = (uint8_t)((quantity + 7U) / 8U);
    ctx->response[0] = ctx->slave;
    ctx->response[1] = adu[1];
    ctx->response[2] = byte_count;
    memset(&ctx->response[3], 0, byte_count);
    for(i = 0U; i < quantity; i++)
    {
        if(table[offset + i] != 0U)
            ctx->response[3U + i / 8U] |= (uint8_t)(1U << (i % 8U));
    }
    return modbus_send(ctx, (uint16_t)(3U + byte_count));
}

static modbus_process_result_t modbus_read_registers(modbus_t *ctx, const uint8_t *adu,
                                                      bool inputs, bool broadcast)
{
    modbus_mapping_t *map = ctx->mapping;
    uint16_t address = modbus_get_u16(&adu[2]);
    uint16_t quantity = modbus_get_u16(&adu[4]);
    uint16_t start = inputs ? map->start_input_registers : map->start_registers;
    uint16_t count = inputs ? map->nb_input_registers : map->nb_registers;
    uint16_t *table = inputs ? map->tab_input_registers : map->tab_registers;
    uint16_t offset;
    uint16_t i;

    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    if(quantity == 0U || quantity > MODBUS_MAX_READ_REGISTERS)
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, false);
    if(table == NULL || !modbus_range_offset(address, quantity, start, count, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, false);

    ctx->response[0] = ctx->slave;
    ctx->response[1] = adu[1];
    ctx->response[2] = (uint8_t)(quantity * 2U);
    for(i = 0U; i < quantity; i++)
        modbus_put_u16(&ctx->response[3U + i * 2U], table[offset + i]);
    return modbus_send(ctx, (uint16_t)(3U + quantity * 2U));
}

static modbus_process_result_t modbus_write_single_coil(modbus_t *ctx, const uint8_t *adu,
                                                         bool broadcast)
{
    uint16_t address = modbus_get_u16(&adu[2]);
    uint16_t value = modbus_get_u16(&adu[4]);
    uint16_t offset;

    if(value != 0x0000U && value != 0xFF00U)
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
    if(ctx->mapping->tab_bits == NULL ||
       !modbus_range_offset(address, 1U, ctx->mapping->start_bits,
                            ctx->mapping->nb_bits, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, broadcast);

    ctx->mapping->tab_bits[offset] = (value == 0xFF00U) ? 1U : 0U;
    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    memcpy(ctx->response, adu, 6U);
    return modbus_send(ctx, 6U);
}

static modbus_process_result_t modbus_write_single_register(modbus_t *ctx,
                                                             const uint8_t *adu,
                                                             bool broadcast)
{
    uint16_t address = modbus_get_u16(&adu[2]);
    uint16_t offset;

    if(ctx->mapping->tab_registers == NULL ||
       !modbus_range_offset(address, 1U, ctx->mapping->start_registers,
                            ctx->mapping->nb_registers, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, broadcast);

    ctx->mapping->tab_registers[offset] = modbus_get_u16(&adu[4]);
    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    memcpy(ctx->response, adu, 6U);
    return modbus_send(ctx, 6U);
}

static modbus_process_result_t modbus_write_multiple_coils(modbus_t *ctx, const uint8_t *adu,
                                                            uint16_t length, bool broadcast)
{
    uint16_t address = modbus_get_u16(&adu[2]);
    uint16_t quantity = modbus_get_u16(&adu[4]);
    uint8_t byte_count = adu[6];
    uint16_t offset;
    uint16_t i;

    if(quantity == 0U || quantity > MODBUS_MAX_WRITE_BITS ||
       byte_count != (uint8_t)((quantity + 7U) / 8U) ||
       length != (uint16_t)(9U + byte_count))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
    if(ctx->mapping->tab_bits == NULL ||
       !modbus_range_offset(address, quantity, ctx->mapping->start_bits,
                            ctx->mapping->nb_bits, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, broadcast);

    for(i = 0U; i < quantity; i++)
        ctx->mapping->tab_bits[offset + i] = (uint8_t)((adu[7U + i / 8U] >> (i % 8U)) & 1U);
    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    memcpy(ctx->response, adu, 6U);
    return modbus_send(ctx, 6U);
}

static modbus_process_result_t modbus_write_multiple_registers(modbus_t *ctx,
                                                                const uint8_t *adu,
                                                                uint16_t length,
                                                                bool broadcast)
{
    uint16_t address = modbus_get_u16(&adu[2]);
    uint16_t quantity = modbus_get_u16(&adu[4]);
    uint8_t byte_count = adu[6];
    uint16_t offset;
    uint16_t i;

    if(quantity == 0U || quantity > MODBUS_MAX_WRITE_REGISTERS ||
       byte_count != (uint8_t)(quantity * 2U) || length != (uint16_t)(9U + byte_count))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
    if(ctx->mapping->tab_registers == NULL ||
       !modbus_range_offset(address, quantity, ctx->mapping->start_registers,
                            ctx->mapping->nb_registers, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, broadcast);

    for(i = 0U; i < quantity; i++)
        ctx->mapping->tab_registers[offset + i] = modbus_get_u16(&adu[7U + i * 2U]);
    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    memcpy(ctx->response, adu, 6U);
    return modbus_send(ctx, 6U);
}

static modbus_process_result_t modbus_mask_write_register(modbus_t *ctx,
                                                           const uint8_t *adu,
                                                           bool broadcast)
{
    uint16_t address = modbus_get_u16(&adu[2]);
    uint16_t and_mask = modbus_get_u16(&adu[4]);
    uint16_t or_mask = modbus_get_u16(&adu[6]);
    uint16_t offset;
    uint16_t value;

    if(ctx->mapping->tab_registers == NULL ||
       !modbus_range_offset(address, 1U, ctx->mapping->start_registers,
                            ctx->mapping->nb_registers, &offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, broadcast);

    value = ctx->mapping->tab_registers[offset];
    ctx->mapping->tab_registers[offset] = (uint16_t)((value & and_mask) | (or_mask & ~and_mask));
    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    memcpy(ctx->response, adu, 8U);
    return modbus_send(ctx, 8U);
}

static modbus_process_result_t modbus_write_read_registers(modbus_t *ctx,
                                                            const uint8_t *adu,
                                                            uint16_t length,
                                                            bool broadcast)
{
    uint16_t read_address = modbus_get_u16(&adu[2]);
    uint16_t read_quantity = modbus_get_u16(&adu[4]);
    uint16_t write_address = modbus_get_u16(&adu[6]);
    uint16_t write_quantity = modbus_get_u16(&adu[8]);
    uint8_t byte_count = adu[10];
    uint16_t read_offset;
    uint16_t write_offset;
    uint16_t i;

    if(broadcast)
        return MODBUS_PROCESS_BROADCAST;
    if(read_quantity == 0U || read_quantity > MODBUS_MAX_READ_REGISTERS ||
       write_quantity == 0U || write_quantity > 121U ||
       byte_count != (uint8_t)(write_quantity * 2U) ||
       length != (uint16_t)(13U + byte_count))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, false);
    if(ctx->mapping->tab_registers == NULL ||
       !modbus_range_offset(read_address, read_quantity, ctx->mapping->start_registers,
                            ctx->mapping->nb_registers, &read_offset) ||
       !modbus_range_offset(write_address, write_quantity, ctx->mapping->start_registers,
                            ctx->mapping->nb_registers, &write_offset))
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, false);

    for(i = 0U; i < write_quantity; i++)
        ctx->mapping->tab_registers[write_offset + i] = modbus_get_u16(&adu[11U + i * 2U]);
    ctx->response[0] = ctx->slave;
    ctx->response[1] = adu[1];
    ctx->response[2] = (uint8_t)(read_quantity * 2U);
    for(i = 0U; i < read_quantity; i++)
        modbus_put_u16(&ctx->response[3U + i * 2U], ctx->mapping->tab_registers[read_offset + i]);
    return modbus_send(ctx, (uint16_t)(3U + read_quantity * 2U));
}

void modbus_mapping_init(modbus_mapping_t *mapping,
                         uint8_t *bits, uint16_t start_bits, uint16_t nb_bits,
                         uint8_t *input_bits, uint16_t start_input_bits, uint16_t nb_input_bits,
                         uint16_t *registers, uint16_t start_registers, uint16_t nb_registers,
                         uint16_t *input_registers, uint16_t start_input_registers,
                         uint16_t nb_input_registers)
{
    if(mapping == NULL)
        return;

    mapping->tab_bits = bits;
    mapping->start_bits = start_bits;
    mapping->nb_bits = nb_bits;
    mapping->tab_input_bits = input_bits;
    mapping->start_input_bits = start_input_bits;
    mapping->nb_input_bits = nb_input_bits;
    mapping->tab_registers = registers;
    mapping->start_registers = start_registers;
    mapping->nb_registers = nb_registers;
    mapping->tab_input_registers = input_registers;
    mapping->start_input_registers = start_input_registers;
    mapping->nb_input_registers = nb_input_registers;
}

int modbus_rtu_slave_init(modbus_t *ctx, uint8_t slave, modbus_mapping_t *mapping,
                          modbus_send_fn send, void *send_arg)
{
    if(ctx == NULL || mapping == NULL || send == NULL || slave == 0U || slave > 247U)
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->slave = slave;
    ctx->mapping = mapping;
    ctx->send = send;
    ctx->send_arg = send_arg;
    return 0;
}

modbus_process_result_t modbus_rtu_slave_process(modbus_t *ctx,
                                                  const uint8_t *adu,
                                                  uint16_t length)
{
    bool broadcast;

    if(ctx == NULL || adu == NULL || ctx->mapping == NULL || ctx->send == NULL)
        return MODBUS_PROCESS_ERROR;
    ctx->last_error = MODBUS_ERROR_NONE;
    if(length < 4U || length > MODBUS_RTU_MAX_ADU_LENGTH)
    {
        ctx->last_error = MODBUS_ERROR_LENGTH;
        return MODBUS_PROCESS_ERROR;
    }
    if(adu[0] != ctx->slave && adu[0] != MODBUS_BROADCAST_ADDRESS)
    {
        ctx->stats.ignored_frames++;
        return MODBUS_PROCESS_IGNORED;
    }
    if(!modbus_rtu_crc_valid(adu, length))
    {
        ctx->last_error = MODBUS_ERROR_CRC;
        ctx->stats.crc_errors++;
        return MODBUS_PROCESS_ERROR;
    }

    broadcast = adu[0] == MODBUS_BROADCAST_ADDRESS;
    ctx->stats.rx_frames++;

    switch(adu[1])
    {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS:
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS:
    case MODBUS_FC_WRITE_SINGLE_COIL:
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        if(length != 8U)
            return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
        break;
    case MODBUS_FC_READ_EXCEPTION_STATUS:
        if(length != 4U)
            return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
        break;
    case MODBUS_FC_MASK_WRITE_REGISTER:
        if(length != 10U)
            return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
        break;
    case MODBUS_FC_WRITE_AND_READ_REGISTERS:
        if(length < 15U)
            return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
        break;
    case MODBUS_FC_WRITE_MULTIPLE_COILS:
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        if(length < 9U)
            return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
        break;
    case MODBUS_FC_REPORT_SLAVE_ID:
        if(length != 4U)
            return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, broadcast);
        break;
    default:
        return modbus_exception(ctx, adu[1], MODBUS_EXCEPTION_ILLEGAL_FUNCTION, broadcast);
    }

    switch(adu[1])
    {
    case MODBUS_FC_READ_COILS:
        return modbus_read_bits(ctx, adu, false, broadcast);
    case MODBUS_FC_READ_DISCRETE_INPUTS:
        return modbus_read_bits(ctx, adu, true, broadcast);
    case MODBUS_FC_READ_HOLDING_REGISTERS:
        return modbus_read_registers(ctx, adu, false, broadcast);
    case MODBUS_FC_READ_INPUT_REGISTERS:
        return modbus_read_registers(ctx, adu, true, broadcast);
    case MODBUS_FC_WRITE_SINGLE_COIL:
        return modbus_write_single_coil(ctx, adu, broadcast);
    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        return modbus_write_single_register(ctx, adu, broadcast);
    case MODBUS_FC_WRITE_MULTIPLE_COILS:
        return modbus_write_multiple_coils(ctx, adu, length, broadcast);
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        return modbus_write_multiple_registers(ctx, adu, length, broadcast);
    case MODBUS_FC_MASK_WRITE_REGISTER:
        return modbus_mask_write_register(ctx, adu, broadcast);
    case MODBUS_FC_WRITE_AND_READ_REGISTERS:
        return modbus_write_read_registers(ctx, adu, length, broadcast);
    case MODBUS_FC_READ_EXCEPTION_STATUS:
        if(broadcast)
            return MODBUS_PROCESS_BROADCAST;
        ctx->response[0] = ctx->slave;
        ctx->response[1] = adu[1];
        ctx->response[2] = 0U;
        return modbus_send(ctx, 3U);
    case MODBUS_FC_REPORT_SLAVE_ID:
        if(broadcast)
            return MODBUS_PROCESS_BROADCAST;
        ctx->response[0] = ctx->slave;
        ctx->response[1] = adu[1];
        ctx->response[2] = 3U;
        ctx->response[3] = ctx->slave;
        ctx->response[4] = 0xFFU;
        ctx->response[5] = 0U;
        return modbus_send(ctx, 6U);
    default:
        return MODBUS_PROCESS_ERROR;
    }
}

modbus_error_t modbus_get_last_error(const modbus_t *ctx)
{
    return (ctx != NULL) ? ctx->last_error : MODBUS_ERROR_ARGUMENT;
}

void modbus_get_stats(const modbus_t *ctx, modbus_stats_t *stats)
{
    if(ctx != NULL && stats != NULL)
        *stats = ctx->stats;
}

void modbus_clear_stats(modbus_t *ctx)
{
    if(ctx != NULL)
        memset(&ctx->stats, 0, sizeof(ctx->stats));
}
