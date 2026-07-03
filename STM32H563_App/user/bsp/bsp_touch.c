#include "bsp_touch.h"

#include <string.h>

#include "bsp_lcd.h"
#include "i2c.h"

#define FT6336U_I2C_ADDR_7BIT       0x38U
#define FT6336U_I2C_ADDR            (FT6336U_I2C_ADDR_7BIT << 1)

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

static int ft6336u_read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0U)
        return -1;

    if(HAL_I2C_Mem_Read(&hi2c1,
                        FT6336U_I2C_ADDR,
                        reg,
                        I2C_MEMADD_SIZE_8BIT,
                        data,
                        len,
                        50U) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

static uint8_t ft6336u_read_id(uint8_t reg)
{
    uint8_t value = 0U;

    (void)ft6336u_read_reg(reg, &value, 1U);
    return value;
}

uint8_t bsp_touch_int_active(void)
{
    return (HAL_GPIO_ReadPin(BSP_LCD_TOUCH_INT_PORT, BSP_LCD_TOUCH_INT_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

int bsp_touch_init(void)
{
    g_touch_present = 0U;
    g_touch_initialized = 0U;
    g_touch_i2c_initialized = 0U;
    g_touch_chip_id = 0U;
    g_touch_vendor_id = 0U;

    HAL_GPIO_WritePin(BSP_LCD_TOUCH_RESET_PORT, BSP_LCD_TOUCH_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(BSP_LCD_TOUCH_RESET_PORT, BSP_LCD_TOUCH_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(200U);

    g_touch_initialized = 1U;
    return 0;
}

static int bsp_touch_probe(void)
{
    if(g_touch_initialized == 0U)
    {
        if(bsp_touch_init() != 0)
            return -1;
    }

    if(g_touch_i2c_initialized == 0U)
    {
        MX_I2C1_Init();
        g_touch_i2c_initialized = 1U;
    }

    if(HAL_I2C_IsDeviceReady(&hi2c1,
                             FT6336U_I2C_ADDR,
                             FT6336U_READY_TRIALS,
                             FT6336U_READY_TIMEOUT_MS) != HAL_OK)
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
        state->x = (uint16_t)(((uint16_t)(buf[1] & 0x0FU) << 8) | buf[2]);
        state->y = (uint16_t)(((uint16_t)(buf[3] & 0x0FU) << 8) | buf[4]);

        if(state->x >= BSP_TOUCH_WIDTH)
            state->x = BSP_TOUCH_WIDTH - 1U;
        if(state->y >= BSP_TOUCH_HEIGHT)
            state->y = BSP_TOUCH_HEIGHT - 1U;
    }

    return 0;
}
