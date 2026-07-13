/**
 * @file ld_modbus_rtu_framer.h
 * @brief Platform-independent Modbus RTU T1.5/T3.5 receive framer.
 *
 * This optional module belongs to the ld_modbus distribution but is not a
 * dependency of the RTU/TCP codec or client/server core. The platform port
 * supplies baud information, byte-completion timestamps, and current time.
 */

#ifndef LD_MODBUS_RTU_FRAMER_H
#define LD_MODBUS_RTU_FRAMER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Receive-framing diagnostics retained across resets. */
typedef struct
{
    uint32_t frames_completed;
    uint32_t t15_violations;
    uint32_t overflow;
    uint32_t dropped_while_ready;
} ld_modbus_rtu_framer_diag_t;

/** @brief Caller-owned RTU receive state and double-buffer bindings. */
typedef struct
{
    uint8_t *active_buffer;
    uint8_t *ready_buffer;
    uint16_t capacity;
    uint16_t active_length;
    uint16_t ready_length;
    uint32_t char_us;
    uint32_t t15_us;
    uint32_t t35_us;
    uint32_t last_byte_us;
    bool active_open;
    bool ready_available;
    bool discard_until_t35;
    ld_modbus_rtu_framer_diag_t diag;
} ld_modbus_rtu_framer_t;

/**
 * @brief Calculate one complete serial-character duration, rounded up.
 * @param baud_rate Current UART baud rate in bits per second.
 * @param bits_per_char Total start, data, parity, and stop bits per character.
 * @return Character duration in microseconds, or zero for invalid arguments.
 */
uint32_t ld_modbus_rtu_char_time_us(uint32_t baud_rate,
                                    uint8_t bits_per_char);

/**
 * @brief Calculate the Modbus RTU T1.5 and T3.5 silent intervals.
 * @param baud_rate Current UART baud rate in bits per second.
 * @param bits_per_char Total serial bits per character.
 * @param t15_us Receives T1.5 in microseconds.
 * @param t35_us Receives T3.5 in microseconds.
 * @note Baud rates above 19200 use the recommended fixed 750/1750 us values.
 */
void ld_modbus_rtu_calculate_gaps(uint32_t baud_rate,
                                  uint8_t bits_per_char,
                                  uint32_t *t15_us,
                                  uint32_t *t35_us);

/**
 * @brief Initialize a framer with two caller-owned buffers of equal capacity.
 * @return True when arguments and calculated timing values are valid.
 */
bool ld_modbus_rtu_framer_init(ld_modbus_rtu_framer_t *ctx,
                               uint8_t *active_buffer,
                               uint8_t *ready_buffer,
                               uint16_t capacity,
                               uint32_t baud_rate,
                               uint8_t bits_per_char);

/**
 * @brief Feed one byte and its end-of-character timestamp.
 * @param timestamp_us Wrapping microsecond time captured when this character
 * finished reception. Unsigned subtraction makes 32-bit timer wrap safe.
 * @note A silence greater than T1.5 but less than T3.5 discards the complete
 * invalid stream until a new T3.5 silent interval is observed.
 */
void ld_modbus_rtu_framer_on_byte(ld_modbus_rtu_framer_t *ctx,
                                  uint8_t byte,
                                  uint32_t timestamp_us);

/**
 * @brief Commit an open frame after T3.5 silence from the last completed byte.
 * @param now_us Current wrapping microsecond time from the same clock source.
 */
void ld_modbus_rtu_framer_poll(ld_modbus_rtu_framer_t *ctx,
                               uint32_t now_us);

/**
 * @brief Copy and consume one completed frame.
 * @return True when a frame was copied; false when none is ready or output is
 * too small. A too-small output does not consume the queued frame.
 */
bool ld_modbus_rtu_framer_take(ld_modbus_rtu_framer_t *ctx,
                               uint8_t *out,
                               uint16_t out_capacity,
                               uint16_t *out_length);

/** @brief Drop open and completed data while retaining diagnostics and timing. */
void ld_modbus_rtu_framer_reset(ld_modbus_rtu_framer_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LD_MODBUS_RTU_FRAMER_H */
