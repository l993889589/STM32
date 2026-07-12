/**
 * @file app_shell.h
 * @brief Multi-transport application shell lifecycle and ingress interface.
 */

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

/** @brief Initialize static shell, LDC, semaphore, and thread resources. */
int app_shell_init(void);
/** @brief Bind one transport writer without transferring its context ownership. */
int app_shell_bind_transport(app_shell_transport_t transport, app_shell_write_fn write, void *arg);
/** @brief Notify the shell that a bound transport is connected. */
void app_shell_connected(app_shell_transport_t transport);
/** @brief Notify the shell that a bound transport is disconnected. */
void app_shell_disconnected(app_shell_transport_t transport);
/** @brief Wake the shell when queued input or a pending banner exists. */
void app_shell_poll(void);
/** @brief Feed one bounded transport fragment into the shell ingress queue. */
bool app_shell_feed(app_shell_transport_t transport, const uint8_t *data, uint16_t length);
/** @brief Process one complete shell input fragment in shell context. */
void app_shell_input(app_shell_transport_t transport, const uint8_t *data, uint16_t length);
/** @brief Report whether a fragment belongs to an active shell session. */
bool app_shell_accepts_input(app_shell_transport_t transport, const uint8_t *data, uint16_t length);

#endif /* APP_SHELL_H */
