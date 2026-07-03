/*
 * STM32 Modbus RTU slave core derived from libmodbus concepts.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef MODBUS_H
#define MODBUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_RTU_MAX_ADU_LENGTH          256U
#define MODBUS_BROADCAST_ADDRESS            0U
#define MODBUS_MAX_READ_BITS             2000U
#define MODBUS_MAX_WRITE_BITS            1968U
#define MODBUS_MAX_READ_REGISTERS         125U
#define MODBUS_MAX_WRITE_REGISTERS        123U

#define MODBUS_FC_READ_COILS               0x01U
#define MODBUS_FC_READ_DISCRETE_INPUTS     0x02U
#define MODBUS_FC_READ_HOLDING_REGISTERS   0x03U
#define MODBUS_FC_READ_INPUT_REGISTERS     0x04U
#define MODBUS_FC_WRITE_SINGLE_COIL        0x05U
#define MODBUS_FC_WRITE_SINGLE_REGISTER    0x06U
#define MODBUS_FC_READ_EXCEPTION_STATUS    0x07U
#define MODBUS_FC_WRITE_MULTIPLE_COILS     0x0FU
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10U
#define MODBUS_FC_REPORT_SLAVE_ID           0x11U
#define MODBUS_FC_MASK_WRITE_REGISTER       0x16U
#define MODBUS_FC_WRITE_AND_READ_REGISTERS  0x17U

typedef enum
{
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS = 0x02,
    MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE = 0x03,
    MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE = 0x04
} modbus_exception_t;

typedef enum
{
    MODBUS_ERROR_NONE = 0,
    MODBUS_ERROR_ARGUMENT,
    MODBUS_ERROR_LENGTH,
    MODBUS_ERROR_CRC,
    MODBUS_ERROR_ADDRESS,
    MODBUS_ERROR_TRANSPORT
} modbus_error_t;

typedef enum
{
    MODBUS_PROCESS_ERROR = -1,
    MODBUS_PROCESS_IGNORED = 0,
    MODBUS_PROCESS_REPLIED = 1,
    MODBUS_PROCESS_BROADCAST = 2
} modbus_process_result_t;

typedef int (*modbus_send_fn)(const uint8_t *data, uint16_t length, void *arg);

typedef struct
{
    uint16_t start_bits;
    uint16_t nb_bits;
    uint16_t start_input_bits;
    uint16_t nb_input_bits;
    uint16_t start_registers;
    uint16_t nb_registers;
    uint16_t start_input_registers;
    uint16_t nb_input_registers;
    uint8_t *tab_bits;
    uint8_t *tab_input_bits;
    uint16_t *tab_registers;
    uint16_t *tab_input_registers;
} modbus_mapping_t;

typedef struct
{
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t ignored_frames;
    uint32_t crc_errors;
    uint32_t exceptions;
    uint32_t transport_errors;
} modbus_stats_t;

typedef struct
{
    uint8_t slave;
    modbus_mapping_t *mapping;
    modbus_send_fn send;
    void *send_arg;
    modbus_error_t last_error;
    modbus_stats_t stats;
    uint8_t response[MODBUS_RTU_MAX_ADU_LENGTH];
} modbus_t;

void modbus_mapping_init(modbus_mapping_t *mapping,
                         uint8_t *bits, uint16_t start_bits, uint16_t nb_bits,
                         uint8_t *input_bits, uint16_t start_input_bits, uint16_t nb_input_bits,
                         uint16_t *registers, uint16_t start_registers, uint16_t nb_registers,
                         uint16_t *input_registers, uint16_t start_input_registers,
                         uint16_t nb_input_registers);

int modbus_rtu_slave_init(modbus_t *ctx,
                          uint8_t slave,
                          modbus_mapping_t *mapping,
                          modbus_send_fn send,
                          void *send_arg);
modbus_process_result_t modbus_rtu_slave_process(modbus_t *ctx,
                                                  const uint8_t *adu,
                                                  uint16_t length);
uint16_t modbus_rtu_crc(const uint8_t *data, uint16_t length);
bool modbus_rtu_crc_valid(const uint8_t *adu, uint16_t length);
modbus_error_t modbus_get_last_error(const modbus_t *ctx);
void modbus_get_stats(const modbus_t *ctx, modbus_stats_t *stats);
void modbus_clear_stats(modbus_t *ctx);

void modbus_set_bits_from_byte(uint8_t *dest, int idx, uint8_t value);
void modbus_set_bits_from_bytes(uint8_t *dest, int idx, unsigned int nb_bits,
                                const uint8_t *tab_byte);
uint8_t modbus_get_byte_from_bits(const uint8_t *src, int idx, unsigned int nb_bits);
float modbus_get_float_abcd(const uint16_t *src);
float modbus_get_float_dcba(const uint16_t *src);
float modbus_get_float_badc(const uint16_t *src);
float modbus_get_float_cdab(const uint16_t *src);
float modbus_get_float(const uint16_t *src);
void modbus_set_float_abcd(float value, uint16_t *dest);
void modbus_set_float_dcba(float value, uint16_t *dest);
void modbus_set_float_badc(float value, uint16_t *dest);
void modbus_set_float_cdab(float value, uint16_t *dest);
void modbus_set_float(float value, uint16_t *dest);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_H */
