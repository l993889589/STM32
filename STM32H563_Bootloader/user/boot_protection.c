/* STM32H563 bank-1 WRP policy for the 128 KiB Boot region. */
#include "boot_protection.h"

#include "main.h"
#include <string.h>

#if defined(OB_WRP_SECTOR_0TO3)
#define BOOT_PROTECTION_REQUIRED_MASK \
    (OB_WRP_SECTOR_0TO3 | OB_WRP_SECTOR_4TO7 | \
     OB_WRP_SECTOR_8TO11 | OB_WRP_SECTOR_12TO15)
#else
#define BOOT_PROTECTION_REQUIRED_MASK 0x0000FFFFUL
#endif

void boot_protection_get_status(boot_protection_status_t *status)
{
    FLASH_OBProgramInitTypeDef option_bytes;

    if(status == NULL)
    {
        return;
    }
    memset(&option_bytes, 0, sizeof(option_bytes));
    option_bytes.Banks = FLASH_BANK_1;
    HAL_FLASHEx_OBGetConfig(&option_bytes);

    status->protected_sector_groups = option_bytes.WRPSector;
    status->required_sector_groups = BOOT_PROTECTION_REQUIRED_MASK;
    status->boot_fully_protected =
        ((option_bytes.WRPSector & BOOT_PROTECTION_REQUIRED_MASK) ==
         BOOT_PROTECTION_REQUIRED_MASK) ? 1U : 0U;
}

#if BOOT_PROTECTION_PROGRAMMING_ENABLED
uint8_t boot_protection_enable(void)
{
    FLASH_OBProgramInitTypeDef option_bytes;
    HAL_StatusTypeDef status;

    memset(&option_bytes, 0, sizeof(option_bytes));
    option_bytes.OptionType = OPTIONBYTE_WRP;
    option_bytes.Banks = FLASH_BANK_1;
    option_bytes.WRPState = OB_WRPSTATE_ENABLE;
    option_bytes.WRPSector = BOOT_PROTECTION_REQUIRED_MASK;

    if(HAL_FLASH_Unlock() != HAL_OK || HAL_FLASH_OB_Unlock() != HAL_OK)
    {
        return 0U;
    }
    status = HAL_FLASHEx_OBProgram(&option_bytes);
    (void)HAL_FLASH_OB_Lock();
    (void)HAL_FLASH_Lock();
    return (status == HAL_OK) ? 1U : 0U;
}
#endif
