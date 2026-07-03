#ifndef LDC_UART4_TEST_H
#define LDC_UART4_TEST_H

#include <stdint.h>

#include "ldc_easy.h"

typedef void (*ldc_uart4_test_packet_handler_t)(const uint8_t *data,
                                                uint16_t len,
                                                void *arg);
typedef void (*ldc_uart4_test_event_handler_t)(ldc_easy_event_t event,
                                               void *arg);

/* Called from ldc_uart4_test_poll(). Data is valid only during the callback. */
void ldc_uart4_test_set_packet_handler(ldc_uart4_test_packet_handler_t handler,
                                       void *arg);

/* Called from the LDC producer context. Keep this handler ISR-safe. */
void ldc_uart4_test_set_event_handler(ldc_uart4_test_event_handler_t handler,
                                      void *arg);

void ldc_uart4_test_init(void);
void ldc_uart4_test_poll(void);
void ldc_uart4_test_tick_ms(uint32_t elapsed_ms);
void ldc_uart4_test_write(const uint8_t *data, uint16_t len);
void ldc_uart4_test_write_text(const char *text);
void ldc_uart4_test_write_u32(uint32_t value);

#endif /* LDC_UART4_TEST_H */
