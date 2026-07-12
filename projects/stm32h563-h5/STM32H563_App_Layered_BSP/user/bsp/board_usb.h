/**
 * @file board_usb.h
 * @brief Private USB FS controller binding for the USBX integration layer.
 */

#ifndef BOARD_USB_H
#define BOARD_USB_H

#include "bsp_status.h"
#include "stm32h5xx_hal.h"

/** @brief Initialize the board USB FS controller and static PMA layout. */
bsp_status_t board_usb_init(void);
/** @brief Start the initialized USB FS controller. */
bsp_status_t board_usb_start(void);
/** @brief Stop USB signaling and mask its IRQ before system Stop mode. */
bsp_status_t board_usb_prepare_stop(void);
/** @brief Restart USB signaling and IRQ delivery after clock restoration. */
bsp_status_t board_usb_resume_after_stop(void);
/** @brief Return the controller handle only to the USBX DCD integration boundary. */
PCD_HandleTypeDef *board_usb_get_handle(void);
/** @brief Dispatch the USB FS vector from ISR context. */
void board_usb_irq_from_isr(void);

#endif /* BOARD_USB_H */
