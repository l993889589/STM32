#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>

int debug_printf(const char *format, ...);
void log_hex_msg(const char *message, const uint8_t *data, int length);

#endif
