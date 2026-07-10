/*
 * Bootloader-owned LDOT firmware recovery transport.
 *
 * The CDC receive thread feeds arbitrary byte chunks to this parser. Commands
 * 32-35 use the same framing as the App uploader, while command 5 resets only
 * after its acknowledgement has been written.
 */
#ifndef BOOT_USB_RECOVERY_H
#define BOOT_USB_RECOVERY_H

#include "ota_firmware_update.h"
#include <stdint.h>

typedef int (*boot_usb_recovery_write_fn)(
    const uint8_t *data,
    uint16_t size,
    void *context);

typedef void (*boot_usb_recovery_reset_fn)(void *context);

/* Bind storage and reset operations; exposed so the parser is host-testable. */
ota_firmware_update_status_t boot_usb_recovery_init(
    const ota_firmware_update_storage_t *storage,
    boot_usb_recovery_reset_fn reset,
    void *reset_context);

/* Bind the production GD25LQ128 and NVIC reset operations. */
ota_firmware_update_status_t boot_usb_recovery_init_default(void);

/* Consume one CDC byte chunk; nonzero means the bytes belong to LDOT. */
uint8_t boot_usb_recovery_feed(
    const uint8_t *data,
    uint32_t size,
    boot_usb_recovery_write_fn write,
    void *write_context);

/* Return current transaction progress for shell and manufacturing diagnostics. */
void boot_usb_recovery_get_progress(
    uint8_t *active,
    uint32_t *target_slot,
    uint32_t *received_size,
    uint32_t *expected_size);

#endif /* BOOT_USB_RECOVERY_H */
