#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <stdbool.h>
#include <stdint.h>

int app_shell_init(void);
void app_shell_connected(void);
void app_shell_disconnected(void);
void app_shell_poll(void);
void app_shell_input(const uint8_t *data, uint16_t length);
bool app_shell_accepts_input(const uint8_t *data, uint16_t length);

#endif /* APP_SHELL_H */
