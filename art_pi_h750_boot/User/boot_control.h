#ifndef BOOT_CONTROL_H
#define BOOT_CONTROL_H

#include <stdint.h>

#define BOOT_EXEC_ADDRESS          0x000000UL
#define BOOT_EXEC_SIZE             0x200000UL
#define BOOT_SLOT_A_ADDRESS        0x200000UL
#define BOOT_SLOT_B_ADDRESS        0x400000UL
#define BOOT_SLOT_SIZE             0x200000UL
#define BOOT_CONTROL_A_ADDRESS     0x600000UL
#define BOOT_CONTROL_B_ADDRESS     0x601000UL

typedef enum
{
    BOOT_CONTROL_OK = 0,
    BOOT_CONTROL_NO_RECORD,
    BOOT_CONTROL_ERROR
} boot_control_status_t;

boot_control_status_t boot_control_prepare(void);

#endif
