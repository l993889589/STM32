/**
 * @file bsp_sensor.h
 * @brief CHPM bindings for the portable AHT20 and DS18B20 drivers.
 */

#ifndef BSP_SENSOR_H
#define BSP_SENSOR_H

#include "aht20.h"
#include "sensor_status.h"

#include "bsp_status.h"

/**
 * @brief Bind the portable sensor contexts to the board I2C and 1-Wire buses.
 * @return BSP initialization status.
 */
bsp_status_t bsp_sensor_init(void);

/**
 * @brief Send the AHT20 soft-reset command.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_reset(void);

/**
 * @brief Send the AHT20 calibration initialization command.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_configure(void);

/**
 * @brief Start one AHT20 environmental conversion.
 * @return Typed sensor or bus status.
 */
sensor_status_t bsp_sensor_aht20_start_measurement(void);

/**
 * @brief Read one completed AHT20 environmental sample.
 * @param measurement Destination for temperature and humidity.
 * @return Typed sensor, readiness, or bus status.
 */
sensor_status_t bsp_sensor_aht20_read_measurement(
    aht20_measurement_t *measurement);

/**
 * @brief Probe the PB0 single-drop bus for the DS18B20.
 * @return Typed presence or bus status.
 */
sensor_status_t bsp_sensor_ds18b20_probe(void);

/**
 * @brief Start one DS18B20 temperature conversion.
 * @return Typed presence or bus status.
 */
sensor_status_t bsp_sensor_ds18b20_start_conversion(void);

/**
 * @brief Read one completed and CRC-checked DS18B20 temperature.
 * @param temperature_c Destination for the Celsius value.
 * @return Typed sensor, CRC, readiness, or bus status.
 */
sensor_status_t bsp_sensor_ds18b20_read_temperature(float *temperature_c);

#endif /* BSP_SENSOR_H */
