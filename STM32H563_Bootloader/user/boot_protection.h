/*
 * Boot write-protection inspection and opt-in manufacturing programming.
 *
 * Runtime builds only inspect option bytes. Programming is compiled out unless
 * a controlled factory target defines BOOT_PROTECTION_PROGRAMMING_ENABLED=1.
 */
#ifndef BOOT_PROTECTION_H
#define BOOT_PROTECTION_H

#include <stdint.h>

#ifndef BOOT_PROTECTION_PROGRAMMING_ENABLED
#define BOOT_PROTECTION_PROGRAMMING_ENABLED 0
#endif

typedef struct
{
    uint32_t protected_sector_groups;
    uint32_t required_sector_groups;
    uint8_t boot_fully_protected;
} boot_protection_status_t;

void boot_protection_get_status(boot_protection_status_t *status);

#if BOOT_PROTECTION_PROGRAMMING_ENABLED
/* Irreversible for normal field operation; use only in a factory target. */
uint8_t boot_protection_enable(void);
#endif

#endif /* BOOT_PROTECTION_H */
