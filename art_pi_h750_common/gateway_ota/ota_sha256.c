/*
 * FIPS 180-4 SHA-256 compression and streaming wrapper.
 *
 * All message length bookkeeping is in bytes until final padding. Inputs may
 * be split at arbitrary boundaries and are never modified by this module.
 */
#include "ota_sha256.h"

#include <string.h>

static const uint32_t ota_sha256_constants[64] =
{
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
};

static uint32_t ota_sha256_rotr(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static uint32_t ota_sha256_get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static void ota_sha256_put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void ota_sha256_transform(ota_sha256_context_t *context, const uint8_t *block)
{
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t index;

    for(index = 0U; index < 16U; index++)
    {
        words[index] = ota_sha256_get_be32(&block[index * 4U]);
    }
    for(index = 16U; index < 64U; index++)
    {
        uint32_t x = words[index - 15U];
        uint32_t y = words[index - 2U];
        uint32_t s0 = ota_sha256_rotr(x, 7U) ^ ota_sha256_rotr(x, 18U) ^ (x >> 3U);
        uint32_t s1 = ota_sha256_rotr(y, 17U) ^ ota_sha256_rotr(y, 19U) ^ (y >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for(index = 0U; index < 64U; index++)
    {
        uint32_t sum1 = ota_sha256_rotr(e, 6U) ^ ota_sha256_rotr(e, 11U) ^
                        ota_sha256_rotr(e, 25U);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choose + ota_sha256_constants[index] + words[index];
        uint32_t sum0 = ota_sha256_rotr(a, 2U) ^ ota_sha256_rotr(a, 13U) ^
                        ota_sha256_rotr(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void ota_sha256_init(ota_sha256_context_t *context)
{
    static const uint32_t initial_state[8] =
    {
        0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
        0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
    };

    if(context != NULL)
    {
        memcpy(context->state, initial_state, sizeof(initial_state));
        context->total_size = 0U;
        context->buffer_size = 0U;
    }
}

void ota_sha256_update(
    ota_sha256_context_t *context,
    const uint8_t *data,
    uint32_t size)
{
    if(context == NULL || (data == NULL && size != 0U))
    {
        return;
    }

    context->total_size += size;
    while(size != 0U)
    {
        uint32_t copy_size = OTA_SHA256_BLOCK_SIZE - context->buffer_size;
        if(copy_size > size)
        {
            copy_size = size;
        }
        memcpy(&context->buffer[context->buffer_size], data, copy_size);
        context->buffer_size += copy_size;
        data += copy_size;
        size -= copy_size;

        if(context->buffer_size == OTA_SHA256_BLOCK_SIZE)
        {
            ota_sha256_transform(context, context->buffer);
            context->buffer_size = 0U;
        }
    }
}

void ota_sha256_finish(
    ota_sha256_context_t *context,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE])
{
    uint64_t bit_count;
    uint32_t index;

    if(context == NULL || digest == NULL)
    {
        return;
    }

    bit_count = context->total_size * 8U;
    context->buffer[context->buffer_size++] = 0x80U;
    if(context->buffer_size > 56U)
    {
        memset(&context->buffer[context->buffer_size], 0,
               OTA_SHA256_BLOCK_SIZE - context->buffer_size);
        ota_sha256_transform(context, context->buffer);
        context->buffer_size = 0U;
    }
    memset(&context->buffer[context->buffer_size], 0, 56U - context->buffer_size);
    for(index = 0U; index < 8U; index++)
    {
        context->buffer[63U - index] = (uint8_t)(bit_count >> (index * 8U));
    }
    ota_sha256_transform(context, context->buffer);

    for(index = 0U; index < 8U; index++)
    {
        ota_sha256_put_be32(&digest[index * 4U], context->state[index]);
    }
    memset(context, 0, sizeof(*context));
}

void ota_sha256_calculate(
    const uint8_t *data,
    uint32_t size,
    uint8_t digest[OTA_SHA256_DIGEST_SIZE])
{
    ota_sha256_context_t context;
    ota_sha256_init(&context);
    ota_sha256_update(&context, data, size);
    ota_sha256_finish(&context, digest);
}
