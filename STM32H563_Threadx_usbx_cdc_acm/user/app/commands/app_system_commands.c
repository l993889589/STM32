#include "app_system_commands.h"

#include <stdio.h>

#include "main.h"

static void app_cmd_version(void *arg)
{
    (void)arg;
    printf("STM32H563 app version 1.0.0\r\n");
}

static void app_cmd_reboot(void *arg)
{
    (void)arg;
    NVIC_SystemReset();
}

void app_system_commands_register(app_command_dispatcher_t *dispatcher)
{
    (void)app_command_register(dispatcher, APP_CMD_VERSION, app_cmd_version, NULL);
    (void)app_command_register(dispatcher, APP_CMD_REBOOT, app_cmd_reboot, NULL);
}
