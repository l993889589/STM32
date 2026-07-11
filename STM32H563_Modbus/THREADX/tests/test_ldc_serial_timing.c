/**
 * @file test_ldc_serial_timing.c
 * @brief Host vectors for protocol-independent UART character-gap calculation.
 */

#include "ldc_core.h"

#include <assert.h>
#include <stdio.h>

/** @brief Verify rounding, line-format width, invalid input, and saturation. */
int main(void)
{
    assert(ldc_serial_silence_us(9600U, 8U, 0U, 1U, 35U) == 3646U);
    assert(ldc_serial_silence_us(19200U, 8U, 1U, 1U, 35U) == 2006U);
    assert(ldc_serial_silence_us(115200U, 8U, 0U, 1U, 35U) == 304U);
    assert(ldc_serial_silence_us(0U, 8U, 0U, 1U, 35U) == 0U);
    assert(ldc_serial_silence_us(1U, 255U, 1U, 255U, 65535U) == UINT32_MAX);
    puts("ldc serial timing tests passed");
    return 0;
}
