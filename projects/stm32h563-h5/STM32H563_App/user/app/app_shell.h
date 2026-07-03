#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    APP_SHELL_TRANSPORT_NONE = 0,
    APP_SHELL_TRANSPORT_UART3,
    APP_SHELL_TRANSPORT_USB_CDC,
    APP_SHELL_TRANSPORT_COUNT
} app_shell_transport_t;

typedef int (*app_shell_write_fn)(const uint8_t *data, uint16_t length, void *arg);

int app_shell_init(void);
int app_shell_bind_transport(app_shell_transport_t transport, app_shell_write_fn write, void *arg);
void app_shell_connected(app_shell_transport_t transport);
void app_shell_disconnected(app_shell_transport_t transport);
void app_shell_poll(void);
bool app_shell_feed(app_shell_transport_t transport, const uint8_t *data, uint16_t length);
void app_shell_input(app_shell_transport_t transport, const uint8_t *data, uint16_t length);
bool app_shell_accepts_input(app_shell_transport_t transport, const uint8_t *data, uint16_t length);

#endif /* APP_SHELL_H */
