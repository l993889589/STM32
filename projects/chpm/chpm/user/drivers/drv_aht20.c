#include "drv_aht20.h"

#include "bsp_i2c_soft.h"

#define AHT20_ADDRESS                   0x38U
#define AHT20_INIT_CMD                  0xE1U
#define AHT20_START_MEASUREMENT_CMD     0xACU
#define AHT20_RESET_CMD                 0xBAU
#define AHT20_STATUS_BUSY               0x80U
#define AHT20_STATUS_CALIBRATED         0x08U

static bool aht20_write_command(uint8_t command,
                                const uint8_t *parameters,
                                uint8_t parameter_count)
{
    bool ok = true;

    i2c_Start();
    i2c_SendByte(AHT20_ADDRESS << 1);
    ok = i2c_WaitAck() == 0U;
    if(ok)
    {
        i2c_SendByte(command);
        ok = i2c_WaitAck() == 0U;
    }
    for(uint8_t i = 0U; ok && i < parameter_count; i++)
    {
        i2c_SendByte(parameters[i]);
        ok = i2c_WaitAck() == 0U;
    }
    i2c_Stop();
    return ok;
}

bool AHT20_Init(void)
{
    static const uint8_t parameters[] = {0x08U, 0x00U};

    return aht20_write_command(AHT20_INIT_CMD, parameters, sizeof(parameters));
}

bool AHT20_Reset(void)
{
    return aht20_write_command(AHT20_RESET_CMD, 0, 0U);
}

bool AHT20_StartMeasurement(void)
{
    static const uint8_t parameters[] = {0x33U, 0x00U};

    return aht20_write_command(AHT20_START_MEASUREMENT_CMD,
                               parameters,
                               sizeof(parameters));
}

bool AHT20_ReadStatus(uint8_t *status)
{
    if(!status)
        return false;

    i2c_Start();
    i2c_SendByte((AHT20_ADDRESS << 1) | 1U);
    if(i2c_WaitAck() != 0U)
    {
        i2c_Stop();
        return false;
    }
    *status = i2c_ReadByte();
    i2c_NAck();
    i2c_Stop();
    return true;
}

bool AHT20_ReadMeasurement(float *temperature, float *humidity)
{
    uint8_t data[6];
    uint32_t raw_humidity;
    uint32_t raw_temperature;
    float measured_temperature;
    float measured_humidity;

    if(!temperature || !humidity)
        return false;

    i2c_Start();
    i2c_SendByte((AHT20_ADDRESS << 1) | 1U);
    if(i2c_WaitAck() != 0U)
    {
        i2c_Stop();
        return false;
    }
    for(uint8_t i = 0U; i < sizeof(data); i++)
    {
        data[i] = i2c_ReadByte();
        if(i + 1U == sizeof(data))
            i2c_NAck();
        else
            i2c_Ack();
    }
    i2c_Stop();

    if((data[0] & AHT20_STATUS_BUSY) != 0U ||
       (data[0] & AHT20_STATUS_CALIBRATED) == 0U)
        return false;

    raw_humidity = (((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) |
                    data[3]) >> 4;
    raw_temperature = ((uint32_t)(data[3] & 0x0FU) << 16) |
                      ((uint32_t)data[4] << 8) |
                      data[5];
    measured_humidity = (float)raw_humidity * 100.0f / 1048576.0f;
    measured_temperature = (float)raw_temperature * 200.0f / 1048576.0f - 50.0f;
    if(measured_temperature < -50.0f || measured_temperature > 150.0f ||
       measured_humidity < 0.0f || measured_humidity > 100.0f)
        return false;

    *temperature = measured_temperature;
    *humidity = measured_humidity;
    return true;
}
