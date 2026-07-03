#include "modbus_rtu.h"



uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;

    if(!data)
        return 0U;

    for(uint16_t pos = 0U; pos < len; pos++)
    {
        crc ^= data[pos];

        for(uint8_t i = 0U; i < 8U; i++)
        {
            if((crc & 0x0001U) != 0U)
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            else
                crc >>= 1;
        }
    }

    return crc;
}

bool modbus_rtu_check_crc(const uint8_t *adu, uint16_t len)
{
    uint16_t calc;
    uint16_t got;

    if(!adu || len < 4U)
        return false;

    calc = modbus_crc16(adu, (uint16_t)(len - 2U));
    /* RTU stores CRC low byte first, then high byte. */
    got = (uint16_t)(((uint16_t)adu[len - 1U] << 8) | adu[len - 2U]);

    return calc == got;
}

uint16_t modbus_rtu_append_crc(uint8_t *adu, uint16_t len, uint16_t max_len)
{
    uint16_t crc;

    if(!adu || (uint16_t)(len + 2U) > max_len)
        return 0U;

    crc = modbus_crc16(adu, len);
    /* Append in wire order: low byte, high byte. */
    adu[len++] = (uint8_t)crc;
    adu[len++] = (uint8_t)(crc >> 8);

    return len;
}
