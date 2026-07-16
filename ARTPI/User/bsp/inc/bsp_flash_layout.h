/**
 * @file bsp_flash_layout.h
 * @brief ART-Pi H750 external-flash layout contract.
 */

#ifndef BSP_FLASH_LAYOUT_H
#define BSP_FLASH_LAYOUT_H

#include "bsp_w25q128.h"

#define BSP_FLASH_WIFI_IMAGE_ADDRESS          0x000000UL
#define BSP_FLASH_WIFI_IMAGE_SIZE             (512UL * 1024UL)
#define BSP_FLASH_WIFI_IMAGE_EXPECTED_CRC32   0x12BACAD0UL

#define BSP_FLASH_BT_IMAGE_ADDRESS            0x080000UL
#define BSP_FLASH_BT_IMAGE_SIZE               (512UL * 1024UL)
#define BSP_FLASH_BT_IMAGE_EXPECTED_CRC32     0x5F4C7B70UL

#define BSP_FLASH_DOWNLOAD_ADDRESS            0x100000UL
#define BSP_FLASH_DOWNLOAD_SIZE               (2UL * 1024UL * 1024UL)

#define BSP_FLASH_EASYFLASH_ADDRESS           0x300000UL
#define BSP_FLASH_EASYFLASH_SIZE              (1UL * 1024UL * 1024UL)

/* Two power-loss-tolerant configuration slots at the end of EasyFlash. */
#define BSP_FLASH_CONFIG_SLOT_A_ADDRESS        \
    (BSP_FLASH_EASYFLASH_ADDRESS + BSP_FLASH_EASYFLASH_SIZE - \
     (2UL * BSP_W25Q128_SECTOR_SIZE))
#define BSP_FLASH_CONFIG_SLOT_B_ADDRESS        \
    (BSP_FLASH_EASYFLASH_ADDRESS + BSP_FLASH_EASYFLASH_SIZE - \
     BSP_W25Q128_SECTOR_SIZE)
#define BSP_FLASH_CONFIG_SLOT_SIZE             BSP_W25Q128_SECTOR_SIZE

#define BSP_FLASH_GATEWAY_OTA_ADDRESS         0x400000UL
#define BSP_FLASH_GATEWAY_OTA_SIZE            (2UL * 1024UL * 1024UL)

#define BSP_FLASH_FILESYSTEM_ADDRESS          0x600000UL

/*
 * The official Resource_16MB.bin leaves the final 4 KiB sector erased.
 * This project reserves it for explicit destructive diagnostics, so the
 * application filesystem must stop before this address.
 */
#define BSP_FLASH_DIAGNOSTIC_ADDRESS          \
    (BSP_W25Q128_TOTAL_SIZE - BSP_W25Q128_SECTOR_SIZE)
#define BSP_FLASH_DIAGNOSTIC_SIZE             BSP_W25Q128_SECTOR_SIZE
#define BSP_FLASH_FILESYSTEM_SIZE             \
    (BSP_FLASH_DIAGNOSTIC_ADDRESS - BSP_FLASH_FILESYSTEM_ADDRESS)

#if ((BSP_FLASH_WIFI_IMAGE_ADDRESS + BSP_FLASH_WIFI_IMAGE_SIZE) != \
     BSP_FLASH_BT_IMAGE_ADDRESS)
#error "ART-Pi Wi-Fi and Bluetooth partitions are not contiguous"
#endif

#if ((BSP_FLASH_BT_IMAGE_ADDRESS + BSP_FLASH_BT_IMAGE_SIZE) != \
     BSP_FLASH_DOWNLOAD_ADDRESS)
#error "ART-Pi Bluetooth and download partitions are not contiguous"
#endif

#if ((BSP_FLASH_DOWNLOAD_ADDRESS + BSP_FLASH_DOWNLOAD_SIZE) != \
     BSP_FLASH_EASYFLASH_ADDRESS)
#error "ART-Pi download and EasyFlash partitions are not contiguous"
#endif

#if ((BSP_FLASH_EASYFLASH_ADDRESS + BSP_FLASH_EASYFLASH_SIZE) != \
     BSP_FLASH_GATEWAY_OTA_ADDRESS)
#error "ART-Pi EasyFlash and gateway OTA partitions are not contiguous"
#endif

#if ((BSP_FLASH_CONFIG_SLOT_B_ADDRESS + BSP_FLASH_CONFIG_SLOT_SIZE) != \
     BSP_FLASH_GATEWAY_OTA_ADDRESS)
#error "ART-Pi configuration slots must end at the EasyFlash boundary"
#endif

#if ((BSP_FLASH_GATEWAY_OTA_ADDRESS + BSP_FLASH_GATEWAY_OTA_SIZE) != \
     BSP_FLASH_FILESYSTEM_ADDRESS)
#error "ART-Pi gateway OTA and filesystem partitions are not contiguous"
#endif

#if ((BSP_FLASH_DIAGNOSTIC_ADDRESS + BSP_FLASH_DIAGNOSTIC_SIZE) != \
     BSP_W25Q128_TOTAL_SIZE)
#error "ART-Pi diagnostic sector must end at the W25Q128 boundary"
#endif

#endif
