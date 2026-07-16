/**
 * @file bsp_touch.c
 * @brief FT6336 touch reset, probe, identification, and coordinate reads.
 */

#include "bsp_touch.h"

#include <string.h>

#include "bsp_gpio.h"
#include "bsp_config.h"
#include "bsp_dwt.h"
#include "bsp_i2c.h"
#define FT6336U_I2C_ADDR_7BIT       0x38U

#define FT6336U_REG_GESTURE         0x01U
#define FT6336U_REG_TD_STATUS       0x02U
#define FT6336U_REG_P1_XH           0x03U
#define FT6336U_REG_VENDOR_ID       0xA3U
#define FT6336U_REG_CHIP_ID_A       0xA6U
#define FT6336U_REG_CHIP_ID_B       0xA8U

#define FT6336U_READY_TRIALS        3U
#define FT6336U_READY_TIMEOUT_MS    20U

static uint8_t g_touch_present;
static uint8_t g_touch_initialized;
static uint8_t g_touch_i2c_initialized;
static uint8_t g_touch_chip_id;
static uint8_t g_touch_vendor_id;
static uint16_t g_touch_last_raw_x;
static uint16_t g_touch_last_raw_y;
static uint8_t g_touch_last_raw_valid;

/** @brief Read one or more consecutive FT6336 registers. */
static int ft6336u_read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0U)
        return -1;

    if(bsp_i2c_memory_read(BOARD_I2C_TOUCH,
                           FT6336U_I2C_ADDR_7BIT,
                           reg,
                           data,
                           len,
                           50U) != BSP_STATUS_OK)
    {
        return -1;
    }

    return 0;
}

/** @brief Read one FT6336 identity register with zero fallback. */
static uint8_t ft6336u_read_id(uint8_t reg)
{
    uint8_t value = 0U;

    (void)ft6336u_read_reg(reg, &value, 1U);
    return value;
}

/** @brief Read the active-low PB14 touch interrupt input. */
uint8_t bsp_touch_int_active(void)
{
    bool is_active = false;
    return bsp_gpio_read(BOARD_GPIO_TOUCH_INTERRUPT, &is_active) == BSP_STATUS_OK &&
           is_active ? 1U : 0U;
}

/** @brief Initialize I2C and perform the PB15 active-low reset sequence. */
int bsp_touch_init(void)
{
    const bsp_i2c_config_t config =
    {
        .bitrate_hz = BOARD_I2C_TOUCH_BITRATE_HZ,
        .rise_time_ns = BOARD_I2C_TOUCH_RISE_TIME_NS,
        .fall_time_ns = BOARD_I2C_TOUCH_FALL_TIME_NS
    };
    bsp_status_t status;

    g_touch_present = 0U;
    g_touch_initialized = 0U;
    g_touch_i2c_initialized = 0U;
    g_touch_chip_id = 0U;
    g_touch_vendor_id = 0U;
    g_touch_last_raw_x = 0U;
    g_touch_last_raw_y = 0U;
    g_touch_last_raw_valid = 0U;

    status = bsp_i2c_init(BOARD_I2C_TOUCH, &config);
    if((status != BSP_STATUS_OK) && (status != BSP_STATUS_ALREADY_INITIALIZED))
    {
        return -1;
    }
    g_touch_i2c_initialized = 1U;

    (void)bsp_gpio_write(BOARD_GPIO_TOUCH_RESET, true);
    bsp_dwt_delay_ms(10U);
    (void)bsp_gpio_write(BOARD_GPIO_TOUCH_RESET, false);
    bsp_dwt_delay_ms(200U);

    g_touch_initialized = 1U;
    return 0;
}

/** @brief Probe the touch controller and capture identity registers. */
static int bsp_touch_probe(void)
{
    if(g_touch_initialized == 0U)
    {
        if(bsp_touch_init() != 0)
            return -1;
    }

    if(g_touch_i2c_initialized == 0U)
    {
        return -1;
    }

    if(bsp_i2c_is_device_ready(BOARD_I2C_TOUCH,
                               FT6336U_I2C_ADDR_7BIT,
                               FT6336U_READY_TRIALS,
                               FT6336U_READY_TIMEOUT_MS) != BSP_STATUS_OK)
    {
        return -1;
    }

    g_touch_vendor_id = ft6336u_read_id(FT6336U_REG_VENDOR_ID);
    g_touch_chip_id = ft6336u_read_id(FT6336U_REG_CHIP_ID_A);
    if(g_touch_chip_id == 0U)
        g_touch_chip_id = ft6336u_read_id(FT6336U_REG_CHIP_ID_B);

    g_touch_present = 1U;
    return 0;
}

/** @brief Read one decoded touch state snapshot. */
int bsp_touch_read(bsp_touch_state_t *state)
{
    uint8_t buf[5];
    uint8_t points;

    if(state == NULL)
        return -1;

    memset(state, 0, sizeof(*state));
    state->present = g_touch_present;
    state->chip_id = g_touch_chip_id;
    state->vendor_id = g_touch_vendor_id;
    state->int_active = bsp_touch_int_active();
    state->last_raw_x = g_touch_last_raw_x;
    state->last_raw_y = g_touch_last_raw_y;
    state->last_raw_valid = g_touch_last_raw_valid;

    if(g_touch_present == 0U)
    {
        if(bsp_touch_probe() != 0)
            return -1;

        state->present = 1U;
        state->chip_id = g_touch_chip_id;
        state->vendor_id = g_touch_vendor_id;
    }

    (void)ft6336u_read_reg(FT6336U_REG_GESTURE, &state->gesture, 1U);

    if(ft6336u_read_reg(FT6336U_REG_TD_STATUS, buf, sizeof(buf)) != 0)
    {
        state->present = 0U;
        g_touch_present = 0U;
        return -1;
    }

    points = buf[0] & 0x0FU;
    if(points > 2U)
        points = 2U;

    state->points = points;
    state->touched = (points != 0U) ? 1U : 0U;
    if(points != 0U)
    {
        state->event = (uint8_t)((buf[1] >> 6) & 0x03U);
        state->raw_x = (uint16_t)(((uint16_t)(buf[1] & 0x0FU) << 8) | buf[2]);
        state->raw_y = (uint16_t)(((uint16_t)(buf[3] & 0x0FU) << 8) | buf[4]);
        g_touch_last_raw_x = state->raw_x;
        g_touch_last_raw_y = state->raw_y;
        g_touch_last_raw_valid = 1U;
        state->last_raw_x = g_touch_last_raw_x;
        state->last_raw_y = g_touch_last_raw_y;
        state->last_raw_valid = 1U;

        /*
         * The FT6336 sensor reports native portrait coordinates while the
         * ST7796 is used in landscape mode (MADCTL.MV). Rotate the point into
         * the 480 x 320 LVGL coordinate system: landscape X follows native Y,
         * and landscape Y is the mirrored native X axis.
         */
        state->x = state->raw_y;
        state->y = (state->raw_x < BSP_TOUCH_HEIGHT) ?
                   (uint16_t)((BSP_TOUCH_HEIGHT - 1U) - state->raw_x) : 0U;

        if(state->x >= BSP_TOUCH_WIDTH)
            state->x = BSP_TOUCH_WIDTH - 1U;
        if(state->y >= BSP_TOUCH_HEIGHT)
            state->y = BSP_TOUCH_HEIGHT - 1U;
    }

    return 0;
}
