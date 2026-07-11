/**
 * @file bsp_usb_device.h
 * @brief Portable USB full-speed device-controller interface.
 */

#ifndef BSP_USB_DEVICE_H
#define BSP_USB_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_status.h"

typedef enum
{
    BSP_USB_ENDPOINT_CONTROL = 0,
    BSP_USB_ENDPOINT_ISOCHRONOUS,
    BSP_USB_ENDPOINT_BULK,
    BSP_USB_ENDPOINT_INTERRUPT
} bsp_usb_endpoint_type_t;

typedef struct
{
    void (*setup_received)(const uint8_t setup[8], void *context);
    void (*out_complete)(uint8_t endpoint_number,
                         uint32_t received_bytes,
                         void *context);
    void (*in_complete)(uint8_t endpoint_number, void *context);
    void (*bus_reset)(void *context);
    void (*suspend)(void *context);
    void (*resume)(void *context);
    void (*connect)(void *context);
    void (*disconnect)(void *context);
    void (*start_of_frame)(void *context);
    void *context;
} bsp_usb_device_handlers_t;

typedef struct
{
    uint32_t setup_packets;
    uint32_t out_completions;
    uint32_t in_completions;
    uint32_t resets;
    uint32_t suspends;
    uint32_t resumes;
    uint32_t connects;
    uint32_t disconnects;
    uint32_t start_of_frames;
    uint32_t errors;
    uint32_t last_hal_error;
    bool started;
    bool connected;
    bool suspended;
} bsp_usb_device_diagnostics_t;

/**
 * Initialize the USB full-speed device controller without connecting it.
 * @param handlers Optional upper-stack interrupt callbacks copied by value.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_init(const bsp_usb_device_handlers_t *handlers);
/**
 * Connect and start the initialized USB device controller.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_start(void);
/**
 * Disconnect and stop the USB device controller.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_stop(void);
/**
 * Program the address assigned by the USB host.
 * @param address USB device address in the range 0..127.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_set_address(uint8_t address);
/**
 * Allocate packet memory and open one endpoint direction.
 * @param endpoint_address Endpoint number with bit 7 set for IN.
 * @param maximum_packet_size Transfer-type-specific full-speed packet size.
 * @param type Portable endpoint transfer type.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_endpoint_open(uint8_t endpoint_address,
                                          uint16_t maximum_packet_size,
                                          bsp_usb_endpoint_type_t type);
/**
 * Close one endpoint direction.
 * @param endpoint_address Endpoint number with bit 7 set for IN.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_endpoint_close(uint8_t endpoint_address);
/**
 * Start a non-blocking IN transfer.
 * @param endpoint_address IN endpoint address.
 * @param data Buffer that remains valid until the IN-complete callback.
 * @param length Transfer size in bytes.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_transmit(uint8_t endpoint_address,
                                     uint8_t *data,
                                     uint32_t length);
/**
 * Arm a non-blocking OUT transfer.
 * @param endpoint_address OUT endpoint address.
 * @param data Buffer that remains valid until the OUT-complete callback.
 * @param capacity Receive capacity in bytes.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_receive(uint8_t endpoint_address,
                                    uint8_t *data,
                                    uint32_t capacity);
/**
 * Set or clear an endpoint halt condition.
 * @param endpoint_address Endpoint direction address.
 * @param is_stalled True to stall; false to clear the stall.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_endpoint_set_stall(uint8_t endpoint_address,
                                               bool is_stalled);
/**
 * Flush pending data for one endpoint direction.
 * @param endpoint_address Endpoint direction address.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_endpoint_flush(uint8_t endpoint_address);
/**
 * Read a USB controller diagnostic snapshot.
 * @param diagnostics Receives counters and link state.
 * @return BSP status.
 */
bsp_status_t bsp_usb_device_get_diagnostics(
    bsp_usb_device_diagnostics_t *diagnostics);

#endif
