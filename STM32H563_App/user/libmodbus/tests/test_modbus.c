#include "modbus.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

static uint8_t tx_data[MODBUS_RTU_MAX_ADU_LENGTH];
static uint16_t tx_length;

static int test_send(const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    memcpy(tx_data, data, length);
    tx_length = length;
    return length;
}

static uint16_t make_request(uint8_t *frame, uint16_t length)
{
    uint16_t crc = modbus_rtu_crc(frame, length);
    frame[length++] = (uint8_t)crc;
    frame[length++] = (uint8_t)(crc >> 8);
    return length;
}

int main(void)
{
    modbus_t ctx;
    modbus_mapping_t mapping;
    uint8_t coils[16] = {0};
    uint16_t registers[16];
    uint8_t request[32];
    uint16_t length;

    for(uint16_t i = 0U; i < 16U; i++)
        registers[i] = i;

    modbus_mapping_init(&mapping,
                        coils, 0U, 16U,
                        NULL, 0U, 0U,
                        registers, 0U, 16U,
                        NULL, 0U, 0U);
    assert(modbus_rtu_slave_init(&ctx, 1U, &mapping, test_send, NULL) == 0);

    {
        uint8_t read_regs[] = {1U, 3U, 0U, 0U, 0U, 3U};
        memcpy(request, read_regs, sizeof(read_regs));
        length = make_request(request, sizeof(read_regs));
        assert(modbus_rtu_slave_process(&ctx, request, length) == MODBUS_PROCESS_REPLIED);
        assert(tx_length == 11U);
        assert(memcmp(tx_data, (uint8_t[]){1U, 3U, 6U, 0U, 0U, 0U, 1U, 0U, 2U}, 9U) == 0);
        assert(modbus_rtu_crc_valid(tx_data, tx_length));
    }

    {
        uint8_t write_reg[] = {1U, 6U, 0U, 2U, 0x12U, 0x34U};
        memcpy(request, write_reg, sizeof(write_reg));
        length = make_request(request, sizeof(write_reg));
        assert(modbus_rtu_slave_process(&ctx, request, length) == MODBUS_PROCESS_REPLIED);
        assert(registers[2] == 0x1234U);
        assert(memcmp(tx_data, request, length) == 0);
    }

    {
        uint8_t write_regs[] = {1U, 0x10U, 0U, 3U, 0U, 2U, 4U, 0xAAU, 0x55U, 0x55U, 0xAAU};
        memcpy(request, write_regs, sizeof(write_regs));
        length = make_request(request, sizeof(write_regs));
        assert(modbus_rtu_slave_process(&ctx, request, length) == MODBUS_PROCESS_REPLIED);
        assert(registers[3] == 0xAA55U && registers[4] == 0x55AAU);
    }

    {
        uint8_t write_coil[] = {1U, 5U, 0U, 1U, 0xFFU, 0U};
        memcpy(request, write_coil, sizeof(write_coil));
        length = make_request(request, sizeof(write_coil));
        assert(modbus_rtu_slave_process(&ctx, request, length) == MODBUS_PROCESS_REPLIED);
        assert(coils[1] == 1U);
    }

    {
        uint8_t bad_address[] = {1U, 3U, 0U, 15U, 0U, 2U};
        memcpy(request, bad_address, sizeof(bad_address));
        length = make_request(request, sizeof(bad_address));
        assert(modbus_rtu_slave_process(&ctx, request, length) == MODBUS_PROCESS_REPLIED);
        assert(tx_data[1] == (uint8_t)(3U | 0x80U));
        assert(tx_data[2] == MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    }

    {
        uint8_t other_slave[] = {2U, 3U, 0U, 0U, 0U, 1U};
        memcpy(request, other_slave, sizeof(other_slave));
        length = make_request(request, sizeof(other_slave));
        assert(modbus_rtu_slave_process(&ctx, request, length) == MODBUS_PROCESS_IGNORED);
    }

    return 0;
}
