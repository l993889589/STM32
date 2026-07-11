/**
 * @file drv_ft6336.c
 * @brief FT6336U touch-controller implementation over the logical touch I2C bus.
 */

#include "drv_ft6336.h"

#include <stddef.h>
#include <string.h>

#include "board_control.h"
#include "bsp_i2c.h"
#include "bsp_time.h"

#define DRV_FT6336_ADDRESS_7BIT       (0x38U)
#define DRV_FT6336_REG_GESTURE        (0x01U)
#define DRV_FT6336_REG_VENDOR_ID      (0xA3U)
#define DRV_FT6336_REG_CHIP_ID_A      (0xA6U)
#define DRV_FT6336_REG_CHIP_ID_B      (0xA8U)
#define DRV_FT6336_TRANSFER_TIMEOUT_MS (20U)
#define DRV_FT6336_RESET_HOLD_MS      (10U)
#define DRV_FT6336_STARTUP_MS         (300U)

static bool drv_ft6336_is_initialized;

/**
 * @brief Read one FT6336U register.
 */
static bsp_status_t drv_ft6336_read_register(uint8_t register_address,
                                             uint8_t *value)
{
    return bsp_i2c_memory_read(BOARD_I2C_TOUCH,
                               DRV_FT6336_ADDRESS_7BIT,
                               register_address,
                               value,
                               1U,
                               DRV_FT6336_TRANSFER_TIMEOUT_MS);
}

/**
 * @brief Decode one six-byte FT6336U touch-point record.
 */
static void drv_ft6336_decode_point(const uint8_t *data,
                                    drv_ft6336_point_t *point)
{
    point->event = (uint8_t)((data[0] >> 6) & 0x03U);
    point->x = (uint16_t)(((uint16_t)(data[0] & 0x0FU) << 8) | data[1]);
    point->touch_id = (uint8_t)((data[2] >> 4) & 0x0FU);
    point->y = (uint16_t)(((uint16_t)(data[2] & 0x0FU) << 8) | data[3]);
}

/**
 * @brief Implement drv_ft6336_init() as documented by its interface contract.
 */
bsp_status_t drv_ft6336_init(drv_ft6336_identity_t *identity)
{
    const bsp_i2c_config_t i2c_config =
    {
        .bitrate_hz = 400000U,
        .rise_time_ns = 300U,
        .fall_time_ns = 100U
    };
    drv_ft6336_identity_t detected = {0};
    bsp_status_t status;

    if(drv_ft6336_is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }

    status = bsp_i2c_init(BOARD_I2C_TOUCH, &i2c_config);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    status = board_control_write(BOARD_CONTROL_TOUCH_RESET, true);
    if(status == BSP_STATUS_OK)
    {
        bsp_time_delay_ms(DRV_FT6336_RESET_HOLD_MS);
        status = board_control_write(BOARD_CONTROL_TOUCH_RESET, false);
    }
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    bsp_time_delay_ms(DRV_FT6336_STARTUP_MS);

    status = drv_ft6336_read_register(DRV_FT6336_REG_VENDOR_ID,
                                      &detected.vendor_id);
    if(status == BSP_STATUS_OK)
    {
        status = drv_ft6336_read_register(DRV_FT6336_REG_CHIP_ID_A,
                                          &detected.chip_id);
    }
    if((status == BSP_STATUS_OK) &&
       ((detected.chip_id == 0U) || (detected.chip_id == UINT8_MAX)))
    {
        status = drv_ft6336_read_register(DRV_FT6336_REG_CHIP_ID_B,
                                          &detected.chip_id);
    }
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    drv_ft6336_is_initialized = true;
    if(identity != NULL)
    {
        *identity = detected;
    }
    return BSP_STATUS_OK;
}

/**
 * @brief Implement drv_ft6336_interrupt_is_active() as documented by its interface contract.
 */
bsp_status_t drv_ft6336_interrupt_is_active(bool *is_active)
{
    return drv_ft6336_is_initialized ?
           board_control_read(BOARD_CONTROL_TOUCH_INTERRUPT, is_active) :
           BSP_STATUS_NOT_READY;
}

/**
 * @brief Implement drv_ft6336_read_state() as documented by its interface contract.
 */
bsp_status_t drv_ft6336_read_state(drv_ft6336_state_t *state)
{
    uint8_t data[13];
    bsp_status_t status;

    if(state == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!drv_ft6336_is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }

    status = bsp_i2c_memory_read(BOARD_I2C_TOUCH,
                                 DRV_FT6336_ADDRESS_7BIT,
                                 DRV_FT6336_REG_GESTURE,
                                 data,
                                 sizeof(data),
                                 DRV_FT6336_TRANSFER_TIMEOUT_MS);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    (void)memset(state, 0, sizeof(*state));
    state->gesture = data[0];
    state->point_count = data[1] & 0x0FU;
    if(state->point_count > 2U)
    {
        state->point_count = 2U;
    }
    if(state->point_count > 0U)
    {
        drv_ft6336_decode_point(&data[2], &state->points[0]);
    }
    if(state->point_count > 1U)
    {
        drv_ft6336_decode_point(&data[8], &state->points[1]);
    }
    return BSP_STATUS_OK;
}
