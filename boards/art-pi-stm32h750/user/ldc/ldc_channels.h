#ifndef LDC_CHANNELS_H
#define LDC_CHANNELS_H

#include <stdbool.h>
#include <stdint.h>

#include "ldc_core.h"

void debug_uart_queue_init(void);
void debug_uart_queue_tick(void);
void debug_uart_queue_task(void);
int debug_uart_queue_blocking_read(uint8_t *buf, int size, int timeout_ms);
void debug_uart_queue_message_in(unsigned char *buf);
void debug_uart_queue_message_in_ex(unsigned char *buf, unsigned long len);

void usb_uart_queue_init(void);
void usb_uart_queue_tick(void);
void usb_uart_queue_task(void);
int usb_uart_queue_blocking_read(uint8_t *buf, int size, int timeout_ms);
void usb_uart_queue_message_in(unsigned char *buf);
void usb_uart_queue_message_in_ex(unsigned char *buf, unsigned long len);

void dwin_uart_queue_init(void);
void dwin_uart_queue_tick(void);
void dwin_uart_queue_task(void);
int dwin_uart_queue_blocking_read(uint8_t *buf, int size, int timeout_ms);
void dwin_uart_queue_message_in(unsigned char *buf);
void dwin_uart_queue_message_in_ex(unsigned char *buf, unsigned long len);
void dwin_queue_settle(void);
void dwin_queue_abort_frame(void);
bool dwin_queue_get_stats(ldc_stats_t *stats);

void modbus_queue_init(void);
void modbus_queue_tick(void);
void modbus_queue_rx_activity(void);
void modbus_queue_task(void);
int modbus_queue_blocking_read(uint8_t *buf, int size, int timeout_ms);
int modbus_queue_read_bytes(uint8_t *buf, int size, int timeout_ms);
void modbus_queue_message_in(unsigned char *buf);
void modbus_queue_message_in_ex(unsigned char *buf, unsigned long len);
void modbus_queue_settle(void);
void modbus_queue_frame_boundary(void);
void modbus_queue_abort_frame(void);
void modbus_queue_set_baud(uint32_t baud_rate);
bool modbus_queue_get_stats(ldc_stats_t *stats);

void msg_queue_init(void);
void msg_queue_tick(void);
void msg_queue_task(void);
int msg_queue_blocking_read(uint8_t *buf, int size, int timeout_ms);
void msg_queue_message_in(unsigned char *buf);
void msg_queue_message_in_ex(unsigned char *buf, unsigned long len);

#endif
