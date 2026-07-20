#include "debug_log.h"

#include <stdio.h>

int fputc(int c, FILE *stream)
{
    (void)stream;
    return c;
}

int debug_printf(const char *format, ...)
{
    (void)format;
    return 1;
}

static void bytes_to_hex(const uint8_t *source, char *destination, int length)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    int index;

    for(index = 0; index < length; index++)
    {
        destination[index * 2] = hex_digits[source[index] >> 4];
        destination[index * 2 + 1] = hex_digits[source[index] & 0x0fU];
    }
    destination[length * 2] = '\0';
}

void log_hex_msg(const char *message, const uint8_t *data, int length)
{
    char converted[128] = {0};

    if(message == NULL || length < 0 || length >= 64)
        return;
    if(length > 0 && data == NULL)
        return;

    if(length > 0)
    {
        bytes_to_hex(data, converted, length);
        printf("%s:%d,[%s]\r\n", message, length, converted);
    }
    else
    {
        printf("%s:%d,[null]\r\n", message, length);
    }
}
