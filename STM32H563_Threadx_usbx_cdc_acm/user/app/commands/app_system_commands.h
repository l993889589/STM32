#ifndef APP_SYSTEM_COMMANDS_H
#define APP_SYSTEM_COMMANDS_H

#include "app_command_dispatcher.h"

#define APP_CMD_VERSION     0x0001U
#define APP_CMD_REBOOT      0x0002U

void app_system_commands_register(app_command_dispatcher_t *dispatcher);

#endif /* APP_SYSTEM_COMMANDS_H */
