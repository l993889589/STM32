/**
 * @file ld_modbus_rtu_framer.c
 * @brief Platform-independent Modbus RTU T1.5/T3.5 framing implementation.
 */

#include "ld_modbus_rtu_framer.h"

#include <stddef.h>
#include <string.h>

#define LD_MODBUS_RTU_FIXED_GAP_BAUDRATE (19200U)
#define LD_MODBUS_RTU_FIXED_T15_US       (750U)
#define LD_MODBUS_RTU_FIXED_T35_US       (1750U)
#define LD_MODBUS_US_PER_SECOND          (1000000ULL)

/** @brief Divide a 64-bit numerator by a nonzero denominator, rounding up. */
static uint32_t ld_modbus_div_round_up_u64(uint64_t numerator,
                                           uint64_t denominator)
{
    return (uint32_t)((numerator + denominator - 1ULL) / denominator);
}

/** @brief Publish the active frame, or drop it when the ready slot is occupied. */
static void ld_modbus_rtu_framer_commit_active(ld_modbus_rtu_framer_t *ctx)
{
    if((ctx == NULL) || !ctx->active_open || (ctx->active_length == 0U))
    {
        return;
    }

    if(ctx->ready_available)
    {
        ctx->diag.dropped_while_ready++;
    }
    else
    {
        memcpy(ctx->ready_buffer, ctx->active_buffer, ctx->active_length);
        ctx->ready_length = ctx->active_length;
        ctx->ready_available = true;
        ctx->diag.frames_completed++;
    }

    ctx->active_length = 0U;
    ctx->active_open = false;
}

/** @brief Calculate one complete serial-character duration, rounded up. */
uint32_t ld_modbus_rtu_char_time_us(uint32_t baud_rate,
                                    uint8_t bits_per_char)
{
    if((baud_rate == 0U) || (bits_per_char == 0U))
    {
        return 0U;
    }

    return ld_modbus_div_round_up_u64((uint64_t)bits_per_char *
                                          LD_MODBUS_US_PER_SECOND,
                                      baud_rate);
}

/** @brief Calculate the Modbus RTU T1.5 and T3.5 silent intervals. */
void ld_modbus_rtu_calculate_gaps(uint32_t baud_rate,
                                  uint8_t bits_per_char,
                                  uint32_t *t15_us,
                                  uint32_t *t35_us)
{
    uint64_t one_char_numerator;

    if((t15_us == NULL) || (t35_us == NULL))
    {
        return;
    }

    *t15_us = 0U;
    *t35_us = 0U;
    if((baud_rate == 0U) || (bits_per_char == 0U))
    {
        return;
    }

    if(baud_rate > LD_MODBUS_RTU_FIXED_GAP_BAUDRATE)
    {
        *t15_us = LD_MODBUS_RTU_FIXED_T15_US;
        *t35_us = LD_MODBUS_RTU_FIXED_T35_US;
        return;
    }

    one_char_numerator =
        (uint64_t)bits_per_char * LD_MODBUS_US_PER_SECOND;
    *t15_us = ld_modbus_div_round_up_u64(one_char_numerator * 3ULL,
                                         (uint64_t)baud_rate * 2ULL);
    *t35_us = ld_modbus_div_round_up_u64(one_char_numerator * 7ULL,
                                         (uint64_t)baud_rate * 2ULL);
}

/** @brief Initialize a framer with two caller-owned buffers of equal capacity. */
bool ld_modbus_rtu_framer_init(ld_modbus_rtu_framer_t *ctx,
                               uint8_t *active_buffer,
                               uint8_t *ready_buffer,
                               uint16_t capacity,
                               uint32_t baud_rate,
                               uint8_t bits_per_char)
{
    if((ctx == NULL) || (active_buffer == NULL) || (ready_buffer == NULL) ||
       (capacity == 0U) || (baud_rate == 0U) || (bits_per_char == 0U))
    {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->active_buffer = active_buffer;
    ctx->ready_buffer = ready_buffer;
    ctx->capacity = capacity;
    ctx->char_us = ld_modbus_rtu_char_time_us(baud_rate, bits_per_char);
    ld_modbus_rtu_calculate_gaps(baud_rate,
                                 bits_per_char,
                                 &ctx->t15_us,
                                 &ctx->t35_us);
    return (ctx->char_us != 0U) &&
           (ctx->t15_us != 0U) &&
           (ctx->t35_us != 0U);
}

/** @brief Feed one byte and its end-of-character timestamp. */
void ld_modbus_rtu_framer_on_byte(ld_modbus_rtu_framer_t *ctx,
                                  uint8_t byte,
                                  uint32_t timestamp_us)
{
    uint32_t elapsed_us;
    uint32_t completion_t15_us;
    uint32_t completion_t35_us;

    if((ctx == NULL) || (ctx->active_buffer == NULL))
    {
        return;
    }

    elapsed_us = timestamp_us - ctx->last_byte_us;
    completion_t15_us = ctx->char_us + ctx->t15_us;
    completion_t35_us = ctx->char_us + ctx->t35_us;

    if(ctx->discard_until_t35)
    {
        ctx->last_byte_us = timestamp_us;
        if(elapsed_us < completion_t35_us)
        {
            return;
        }
        ctx->discard_until_t35 = false;
    }

    if(ctx->active_open)
    {
        /* Completion-to-completion time includes the current character. */
        if(elapsed_us > completion_t15_us)
        {
            if(elapsed_us >= completion_t35_us)
            {
                ld_modbus_rtu_framer_commit_active(ctx);
            }
            else
            {
                ctx->diag.t15_violations++;
                ctx->active_length = 0U;
                ctx->active_open = false;
                ctx->discard_until_t35 = true;
                ctx->last_byte_us = timestamp_us;
                return;
            }
        }
    }

    if(ctx->active_length >= ctx->capacity)
    {
        ctx->diag.overflow++;
        ctx->active_length = 0U;
        ctx->active_open = false;
        ctx->discard_until_t35 = true;
        ctx->last_byte_us = timestamp_us;
        return;
    }

    ctx->active_buffer[ctx->active_length++] = byte;
    ctx->last_byte_us = timestamp_us;
    ctx->active_open = true;
}

/** @brief Commit an open frame after T3.5 silence. */
void ld_modbus_rtu_framer_poll(ld_modbus_rtu_framer_t *ctx,
                               uint32_t now_us)
{
    if(ctx == NULL)
    {
        return;
    }

    if(ctx->discard_until_t35)
    {
        if((now_us - ctx->last_byte_us) >= ctx->t35_us)
        {
            ctx->discard_until_t35 = false;
        }
        return;
    }

    if(!ctx->active_open)
    {
        return;
    }

    if((now_us - ctx->last_byte_us) >= ctx->t35_us)
    {
        ld_modbus_rtu_framer_commit_active(ctx);
    }
}

/** @brief Copy and consume one completed frame. */
bool ld_modbus_rtu_framer_take(ld_modbus_rtu_framer_t *ctx,
                               uint8_t *out,
                               uint16_t out_capacity,
                               uint16_t *out_length)
{
    if((ctx == NULL) || (out == NULL) || (out_length == NULL))
    {
        return false;
    }

    *out_length = 0U;
    if(!ctx->ready_available || (out_capacity < ctx->ready_length))
    {
        return false;
    }

    memcpy(out, ctx->ready_buffer, ctx->ready_length);
    *out_length = ctx->ready_length;
    ctx->ready_length = 0U;
    ctx->ready_available = false;
    return true;
}

/** @brief Drop open and completed data while retaining diagnostics and timing. */
void ld_modbus_rtu_framer_reset(ld_modbus_rtu_framer_t *ctx)
{
    if(ctx != NULL)
    {
        ctx->active_length = 0U;
        ctx->ready_length = 0U;
        ctx->active_open = false;
        ctx->ready_available = false;
        ctx->discard_until_t35 = false;
    }
}
