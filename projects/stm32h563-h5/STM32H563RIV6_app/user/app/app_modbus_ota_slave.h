/**
 * @file app_modbus_ota_slave.h
 * @brief Modbus function 0x41 firmware-update service for the gateway link.
 */

#ifndef APP_MODBUS_OTA_SLAVE_H
#define APP_MODBUS_OTA_SLAVE_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t change_baud;
    uint8_t reset_target;
    uint32_t baud_rate;
    uint16_t delay_ms;
} app_modbus_ota_slave_action_t;

int app_modbus_ota_slave_init(uint32_t base_baud_rate);

uint8_t app_modbus_ota_slave_process(
    const uint8_t *request_pdu,
    size_t request_length,
    uint8_t *response_pdu,
    size_t response_size,
    size_t *response_length,
    app_modbus_ota_slave_action_t *action);

uint8_t app_modbus_ota_slave_poll(app_modbus_ota_slave_action_t *action);

void app_modbus_ota_slave_baud_changed(uint32_t baud_rate, uint8_t success);

#endif /* APP_MODBUS_OTA_SLAVE_H */
