#include "dispatcher.h"
#include "stm32h7xx.h" // 或你的 MCU 对应头文件

static void cmd_reboot(void)
{
    NVIC_SystemReset();
}

void reboot_cmd_init(void)
{
    dispatcher_register(0x0002, cmd_reboot);
}