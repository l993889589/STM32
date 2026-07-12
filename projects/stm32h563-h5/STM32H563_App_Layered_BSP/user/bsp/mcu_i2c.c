/**
 * @file mcu_i2c.c
 * @brief STM32H5 I2C TIMINGR solver and bounded polling transfers.
 */

#include "mcu_i2c.h"

#include <limits.h>
#include <stddef.h>

/** @brief Conservative minimum timing limits for one I2C speed class. */
typedef struct
{
    uint32_t minimum_low_ns;
    uint32_t minimum_high_ns;
    uint32_t minimum_setup_ns;
} mcu_i2c_limits_t;

/** @brief Return standard, fast, or fast-plus conservative timing limits. */
static bool mcu_i2c_get_limits(uint32_t bitrate_hz, mcu_i2c_limits_t *limits)
{
    if((limits == NULL) || (bitrate_hz == 0U) || (bitrate_hz > 1000000U))
    {
        return false;
    }
    if(bitrate_hz <= 100000U)
    {
        limits->minimum_low_ns = 4700U;
        limits->minimum_high_ns = 4000U;
        limits->minimum_setup_ns = 250U;
    }
    else if(bitrate_hz <= 400000U)
    {
        limits->minimum_low_ns = 1300U;
        limits->minimum_high_ns = 600U;
        limits->minimum_setup_ns = 100U;
    }
    else
    {
        limits->minimum_low_ns = 500U;
        limits->minimum_high_ns = 260U;
        limits->minimum_setup_ns = 50U;
    }
    return true;
}

/** @brief Solve TIMINGR without exceeding the requested bus bitrate. */
static bool mcu_i2c_solve_timing(uint32_t kernel_clock_hz,
                                 const bsp_i2c_config_t *config,
                                 uint32_t *timing_register,
                                 uint32_t *achieved_bitrate_hz)
{
    mcu_i2c_limits_t limits;
    uint64_t target_period_ns;
    uint64_t best_error_ns = UINT64_MAX;
    uint32_t best_timing = 0U;
    uint32_t best_bitrate = 0U;
    uint32_t prescaler;

    if((config == NULL) || (timing_register == NULL) ||
       (achieved_bitrate_hz == NULL) || (kernel_clock_hz == 0U) ||
       !mcu_i2c_get_limits(config->bitrate_hz, &limits))
    {
        return false;
    }

    target_period_ns = 1000000000ULL / config->bitrate_hz;
    for(prescaler = 0U; prescaler <= 15U; prescaler++)
    {
        uint64_t prescaled_ns =
            ((uint64_t)(prescaler + 1U) * 1000000000ULL +
             kernel_clock_hz - 1U) / kernel_clock_hz;
        uint64_t sync_ns =
            (2ULL * 1000000000ULL + kernel_clock_hz - 1U) /
            kernel_clock_hz + 100ULL;
        uint32_t scl_delay = (uint32_t)((limits.minimum_setup_ns +
                                         config->rise_time_ns +
                                         prescaled_ns - 1ULL) / prescaled_ns);
        uint32_t sda_delay = (uint32_t)((config->fall_time_ns +
                                         prescaled_ns - 1ULL) / prescaled_ns);
        uint32_t scl_low;

        if((scl_delay == 0U) || (scl_delay > 16U) || (sda_delay > 15U))
        {
            continue;
        }
        scl_delay--;

        for(scl_low = 0U; scl_low <= 255U; scl_low++)
        {
            uint64_t low_ns = (uint64_t)(scl_low + 1U) * prescaled_ns + sync_ns;
            uint64_t desired_high_ns;
            uint32_t scl_high;
            uint64_t high_ns;
            uint64_t actual_period_ns;
            uint64_t error_ns;
            uint32_t actual_bitrate;

            if(low_ns < limits.minimum_low_ns)
            {
                continue;
            }
            desired_high_ns = target_period_ns > low_ns ?
                              target_period_ns - low_ns : 0U;
            if(desired_high_ns <= sync_ns)
            {
                continue;
            }
            scl_high = (uint32_t)((desired_high_ns - sync_ns +
                                   prescaled_ns - 1ULL) / prescaled_ns);
            if((scl_high == 0U) || (scl_high > 256U))
            {
                continue;
            }
            scl_high--;
            high_ns = (uint64_t)(scl_high + 1U) * prescaled_ns + sync_ns;
            if(high_ns < limits.minimum_high_ns)
            {
                continue;
            }
            actual_period_ns = low_ns + high_ns;
            actual_bitrate = (uint32_t)(1000000000ULL / actual_period_ns);
            if(actual_bitrate > config->bitrate_hz)
            {
                continue;
            }
            error_ns = actual_period_ns - target_period_ns;
            if(error_ns < best_error_ns)
            {
                best_error_ns = error_ns;
                best_bitrate = actual_bitrate;
                best_timing = (prescaler << 28) | (scl_delay << 20) |
                              (sda_delay << 16) | (scl_high << 8) | scl_low;
            }
        }
    }

    if(best_bitrate == 0U)
    {
        return false;
    }
    *timing_register = best_timing;
    *achieved_bitrate_hz = best_bitrate;
    return true;
}

/** @brief Translate HAL completion status to the shared BSP domain. */
static bsp_status_t mcu_i2c_from_hal(HAL_StatusTypeDef status)
{
    if(status == HAL_OK)
    {
        return BSP_STATUS_OK;
    }
    if(status == HAL_TIMEOUT)
    {
        return BSP_STATUS_TIMEOUT;
    }
    if(status == HAL_BUSY)
    {
        return BSP_STATUS_BUSY;
    }
    return BSP_STATUS_IO_ERROR;
}

/** @brief Validate common I2C transfer arguments. */
static bsp_status_t mcu_i2c_validate(const mcu_i2c_context_t *context,
                                     uint8_t address_7bit,
                                     const void *data,
                                     uint32_t length,
                                     uint32_t timeout_ms)
{
    if((context == NULL) || (data == NULL) || (address_7bit > 0x7FU) ||
       (length == 0U) || (length > UINT16_MAX) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    return context->is_initialized ? BSP_STATUS_OK : BSP_STATUS_NOT_READY;
}

/** @brief Implement mcu_i2c_init() with a solved TIMINGR value. */
bsp_status_t mcu_i2c_init(mcu_i2c_context_t *context,
                          I2C_TypeDef *instance,
                          uint32_t kernel_clock_hz,
                          const bsp_i2c_config_t *config)
{
    uint32_t timing_register;
    uint32_t achieved_bitrate_hz;

    if((context == NULL) || (instance == NULL) || (config == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(context->is_initialized)
    {
        return context->handle.Instance == instance ?
               BSP_STATUS_ALREADY_INITIALIZED : BSP_STATUS_CONFLICT;
    }
    if(!mcu_i2c_solve_timing(kernel_clock_hz,
                             config,
                             &timing_register,
                             &achieved_bitrate_hz))
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }

    context->handle.Instance = instance;
    context->handle.Init.Timing = timing_register;
    context->handle.Init.OwnAddress1 = 0U;
    context->handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    context->handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    context->handle.Init.OwnAddress2 = 0U;
    context->handle.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    context->handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    context->handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if((HAL_I2C_Init(&context->handle) != HAL_OK) ||
       (HAL_I2CEx_ConfigAnalogFilter(&context->handle,
                                     I2C_ANALOGFILTER_ENABLE) != HAL_OK) ||
       (HAL_I2CEx_ConfigDigitalFilter(&context->handle, 0U) != HAL_OK))
    {
        return BSP_STATUS_IO_ERROR;
    }

    context->achieved_bitrate_hz = achieved_bitrate_hz;
    context->is_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Implement bounded address readiness probing. */
bsp_status_t mcu_i2c_is_device_ready(mcu_i2c_context_t *context,
                                     uint8_t address_7bit,
                                     uint32_t trials,
                                     uint32_t timeout_ms)
{
    if((context == NULL) || !context->is_initialized ||
       (address_7bit > 0x7FU) || (trials == 0U) || (timeout_ms == 0U))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    return mcu_i2c_from_hal(HAL_I2C_IsDeviceReady(&context->handle,
                                                  (uint16_t)address_7bit << 1,
                                                  trials,
                                                  timeout_ms));
}

/** @brief Implement bounded I2C master transmit. */
bsp_status_t mcu_i2c_write(mcu_i2c_context_t *context,
                           uint8_t address_7bit,
                           const uint8_t *data,
                           uint32_t length,
                           uint32_t timeout_ms)
{
    bsp_status_t status = mcu_i2c_validate(context,
                                           address_7bit,
                                           data,
                                           length,
                                           timeout_ms);
    return status == BSP_STATUS_OK ?
           mcu_i2c_from_hal(HAL_I2C_Master_Transmit(&context->handle,
                                                   (uint16_t)address_7bit << 1,
                                                   (uint8_t *)data,
                                                   (uint16_t)length,
                                                   timeout_ms)) : status;
}

/** @brief Implement bounded I2C master receive. */
bsp_status_t mcu_i2c_read(mcu_i2c_context_t *context,
                          uint8_t address_7bit,
                          uint8_t *data,
                          uint32_t length,
                          uint32_t timeout_ms)
{
    bsp_status_t status = mcu_i2c_validate(context,
                                           address_7bit,
                                           data,
                                           length,
                                           timeout_ms);
    return status == BSP_STATUS_OK ?
           mcu_i2c_from_hal(HAL_I2C_Master_Receive(&context->handle,
                                                  (uint16_t)address_7bit << 1,
                                                  data,
                                                  (uint16_t)length,
                                                  timeout_ms)) : status;
}

/** @brief Implement bounded 8-bit register read. */
bsp_status_t mcu_i2c_memory_read(mcu_i2c_context_t *context,
                                 uint8_t address_7bit,
                                 uint8_t register_address,
                                 uint8_t *data,
                                 uint32_t length,
                                 uint32_t timeout_ms)
{
    bsp_status_t status = mcu_i2c_validate(context,
                                           address_7bit,
                                           data,
                                           length,
                                           timeout_ms);
    return status == BSP_STATUS_OK ?
           mcu_i2c_from_hal(HAL_I2C_Mem_Read(&context->handle,
                                             (uint16_t)address_7bit << 1,
                                             register_address,
                                             I2C_MEMADD_SIZE_8BIT,
                                             data,
                                             (uint16_t)length,
                                             timeout_ms)) : status;
}

/** @brief Implement bounded 8-bit register write. */
bsp_status_t mcu_i2c_memory_write(mcu_i2c_context_t *context,
                                  uint8_t address_7bit,
                                  uint8_t register_address,
                                  const uint8_t *data,
                                  uint32_t length,
                                  uint32_t timeout_ms)
{
    bsp_status_t status = mcu_i2c_validate(context,
                                           address_7bit,
                                           data,
                                           length,
                                           timeout_ms);
    return status == BSP_STATUS_OK ?
           mcu_i2c_from_hal(HAL_I2C_Mem_Write(&context->handle,
                                              (uint16_t)address_7bit << 1,
                                              register_address,
                                              I2C_MEMADD_SIZE_8BIT,
                                              (uint8_t *)data,
                                              (uint16_t)length,
                                              timeout_ms)) : status;
}
