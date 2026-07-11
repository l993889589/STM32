/**
 * @file bsp_fdcan_stm32h5.c
 * @brief STM32H5 FDCAN timing, frame conversion, and interrupt adapter.
 */

#include "bsp_fdcan_stm32h5.h"

#include <string.h>

#define BSP_FDCAN_MAX_BITRATE_ERROR_PPM 10000U

typedef struct
{
    uint16_t prescaler;
    uint16_t segment_1;
    uint16_t segment_2;
    uint16_t sync_jump_width;
    uint16_t sample_point_permille;
    uint32_t bitrate_hz;
    uint32_t bitrate_error_ppm;
    uint32_t score;
    bool valid;
} bsp_fdcan_timing_t;

static bsp_fdcan_stm32h5_context_t *fdcan_contexts[2];

/** @brief Return an absolute difference without signed arithmetic. */
static uint32_t bsp_fdcan_abs_difference(uint32_t left, uint32_t right)
{
    return left >= right ? left - right : right - left;
}

/** @brief Locate the static context registered for a HAL handle. */
static bsp_fdcan_stm32h5_context_t *bsp_fdcan_find_context(
    const FDCAN_HandleTypeDef *handle)
{
    uint32_t index;

    for(index = 0U; index < 2U; ++index)
    {
        if((fdcan_contexts[index] != NULL) &&
           (&fdcan_contexts[index]->handle == handle))
        {
            return fdcan_contexts[index];
        }
    }
    return NULL;
}

/** @brief Register one initialized static context for weak HAL callbacks. */
static bsp_status_t bsp_fdcan_register_context(
    bsp_fdcan_stm32h5_context_t *context)
{
    uint32_t index;

    for(index = 0U; index < 2U; ++index)
    {
        if((fdcan_contexts[index] == NULL) || (fdcan_contexts[index] == context))
        {
            fdcan_contexts[index] = context;
            return BSP_STATUS_OK;
        }
    }
    return BSP_STATUS_CONFLICT;
}

/** @brief Search legal M_CAN timing values for a physical bit-rate request. */
static bsp_status_t bsp_fdcan_solve_timing(uint32_t kernel_clock_hz,
                                          uint32_t requested_bitrate_hz,
                                          uint16_t requested_sample_permille,
                                          uint16_t maximum_prescaler,
                                          uint16_t minimum_segment_1,
                                          uint16_t maximum_segment_1,
                                          uint16_t maximum_segment_2,
                                          uint16_t maximum_sjw,
                                          bsp_fdcan_timing_t *timing)
{
    uint32_t prescaler;
    uint32_t total_quanta;
    bsp_fdcan_timing_t best = {0};

    if((kernel_clock_hz == 0U) || (requested_bitrate_hz == 0U) ||
       (requested_sample_permille < 500U) ||
       (requested_sample_permille > 950U) || (timing == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }

    best.score = UINT32_MAX;
    for(prescaler = 1U; prescaler <= maximum_prescaler; ++prescaler)
    {
        for(total_quanta = 1U + minimum_segment_1 + 1U;
            total_quanta <= 1U + maximum_segment_1 + maximum_segment_2;
            ++total_quanta)
        {
            int32_t delta;
            uint32_t ideal_segment_1 =
                (((uint32_t)requested_sample_permille * total_quanta + 500U) /
                 1000U) - 1U;

            for(delta = -1; delta <= 1; ++delta)
            {
                int32_t signed_segment_1 = (int32_t)ideal_segment_1 + delta;
                uint32_t segment_1;
                uint32_t segment_2;
                uint32_t divisor = prescaler * total_quanta;
                uint32_t bitrate = (uint32_t)(((uint64_t)kernel_clock_hz +
                                               (divisor / 2U)) / divisor);
                uint32_t bitrate_error;
                uint32_t bitrate_error_ppm;
                uint16_t sample_point;
                uint32_t sample_error;
                uint32_t score;

                if(signed_segment_1 < 0)
                {
                    continue;
                }
                segment_1 = (uint32_t)signed_segment_1;
                if((segment_1 < minimum_segment_1) ||
                   (segment_1 > maximum_segment_1) ||
                   (total_quanta <= (1U + segment_1)))
                {
                    continue;
                }
                segment_2 = total_quanta - 1U - segment_1;
                if((segment_2 < 1U) || (segment_2 > maximum_segment_2))
                {
                    continue;
                }
                if(bitrate == 0U)
                {
                    continue;
                }
                bitrate_error = bsp_fdcan_abs_difference(bitrate,
                                                         requested_bitrate_hz);
                bitrate_error_ppm = (uint32_t)(((uint64_t)bitrate_error *
                                                1000000ULL) /
                                               requested_bitrate_hz);
                if(bitrate_error_ppm > BSP_FDCAN_MAX_BITRATE_ERROR_PPM)
                {
                    continue;
                }
                sample_point = (uint16_t)(((1U + segment_1) * 1000U) /
                                          total_quanta);
                sample_error = bsp_fdcan_abs_difference(sample_point,
                                                        requested_sample_permille);
                score = bitrate_error_ppm + (sample_error * 100U);
                if(score < best.score)
                {
                    best.prescaler = (uint16_t)prescaler;
                    best.segment_1 = (uint16_t)segment_1;
                    best.segment_2 = (uint16_t)segment_2;
                    best.sync_jump_width = (uint16_t)(segment_2 < maximum_sjw ?
                                                      segment_2 : maximum_sjw);
                    best.sample_point_permille = sample_point;
                    best.bitrate_hz = bitrate;
                    best.bitrate_error_ppm = bitrate_error_ppm;
                    best.score = score;
                    best.valid = true;
                }
            }
        }
    }

    if(!best.valid)
    {
        return BSP_STATUS_NOT_SUPPORTED;
    }
    *timing = best;
    return BSP_STATUS_OK;
}

/** @brief Convert a public payload length to an M_CAN DLC field. */
static bsp_status_t bsp_fdcan_length_to_dlc(uint8_t length, uint32_t *dlc)
{
    static const uint8_t lengths[16] =
    {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
    };
    uint32_t index;

    if(dlc == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    for(index = 0U; index < 16U; ++index)
    {
        if(lengths[index] == length)
        {
            *dlc = index;
            return BSP_STATUS_OK;
        }
    }
    return BSP_STATUS_INVALID_ARGUMENT;
}

/** @brief Convert an M_CAN DLC field to its payload byte count. */
static uint8_t bsp_fdcan_dlc_to_length(uint32_t dlc)
{
    static const uint8_t lengths[16] =
    {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
    };

    return lengths[dlc & 0x0FU];
}

/** @brief Configure accept-all standard and extended filters into FIFO 0. */
static bsp_status_t bsp_fdcan_configure_filters(FDCAN_HandleTypeDef *handle)
{
    FDCAN_FilterTypeDef filter = {0};

    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0U;
    filter.FilterID2 = 0U;
    filter.IdType = FDCAN_STANDARD_ID;
    if(HAL_FDCAN_ConfigFilter(handle, &filter) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    filter.IdType = FDCAN_EXTENDED_ID;
    if(HAL_FDCAN_ConfigFilter(handle, &filter) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    return HAL_FDCAN_ConfigGlobalFilter(handle,
                                        FDCAN_REJECT,
                                        FDCAN_REJECT,
                                        FDCAN_FILTER_REMOTE,
                                        FDCAN_FILTER_REMOTE) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Initialize an STM32H5 FDCAN context. */
bsp_status_t bsp_fdcan_stm32h5_init(bsp_fdcan_stm32h5_context_t *context,
                                    FDCAN_GlobalTypeDef *instance,
                                    uint32_t kernel_clock_hz,
                                    const bsp_fdcan_config_t *config)
{
    bsp_fdcan_timing_t nominal_timing;
    bsp_fdcan_timing_t data_timing;
    bsp_status_t status;
    uint32_t notifications;

    if((context == NULL) || (instance == NULL) || (config == NULL) ||
       (config->bitrate_switch_enabled && !config->fd_enabled))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(context->is_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }
    status = bsp_fdcan_solve_timing(kernel_clock_hz,
                                    config->nominal_bitrate_hz,
                                    config->nominal_sample_point_permille,
                                    512U, 2U, 256U, 128U, 128U,
                                    &nominal_timing);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = bsp_fdcan_solve_timing(kernel_clock_hz,
                                    config->fd_enabled ? config->data_bitrate_hz :
                                    config->nominal_bitrate_hz,
                                    config->fd_enabled ? config->data_sample_point_permille :
                                    config->nominal_sample_point_permille,
                                    32U, 1U, 32U, 16U, 16U,
                                    &data_timing);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }

    memset(context, 0, sizeof(*context));
    context->handle.Instance = instance;
    context->handle.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    context->handle.Init.FrameFormat = config->fd_enabled ?
                                       (config->bitrate_switch_enabled ?
                                        FDCAN_FRAME_FD_BRS : FDCAN_FRAME_FD_NO_BRS) :
                                       FDCAN_FRAME_CLASSIC;
    context->handle.Init.Mode = FDCAN_MODE_NORMAL;
    context->handle.Init.AutoRetransmission = config->auto_retransmission ? ENABLE : DISABLE;
    context->handle.Init.TransmitPause = DISABLE;
    context->handle.Init.ProtocolException = ENABLE;
    context->handle.Init.NominalPrescaler = nominal_timing.prescaler;
    context->handle.Init.NominalSyncJumpWidth = nominal_timing.sync_jump_width;
    context->handle.Init.NominalTimeSeg1 = nominal_timing.segment_1;
    context->handle.Init.NominalTimeSeg2 = nominal_timing.segment_2;
    context->handle.Init.DataPrescaler = data_timing.prescaler;
    context->handle.Init.DataSyncJumpWidth = data_timing.sync_jump_width;
    context->handle.Init.DataTimeSeg1 = data_timing.segment_1;
    context->handle.Init.DataTimeSeg2 = data_timing.segment_2;
    context->handle.Init.StdFiltersNbr = 1U;
    context->handle.Init.ExtFiltersNbr = 1U;
    context->handle.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    if(HAL_FDCAN_Init(&context->handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    status = bsp_fdcan_configure_filters(&context->handle);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = bsp_fdcan_register_context(context);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    notifications = FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
                    FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
                    FDCAN_IT_ERROR_WARNING |
                    FDCAN_IT_ERROR_PASSIVE |
                    FDCAN_IT_BUS_OFF;
    if(HAL_FDCAN_ActivateNotification(&context->handle,
                                      notifications,
                                      0U) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    if(HAL_FDCAN_Start(&context->handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    context->diagnostics.achieved_nominal_bitrate_hz = nominal_timing.bitrate_hz;
    context->diagnostics.achieved_nominal_sample_point_permille =
        nominal_timing.sample_point_permille;
    context->diagnostics.achieved_data_bitrate_hz = data_timing.bitrate_hz;
    context->diagnostics.achieved_data_sample_point_permille =
        data_timing.sample_point_permille;
    context->fd_enabled = config->fd_enabled;
    context->bitrate_switch_enabled = config->bitrate_switch_enabled;
    context->is_initialized = true;
    context->is_started = true;
    return BSP_STATUS_OK;
}

/** @brief Stop an STM32H5 FDCAN context. */
bsp_status_t bsp_fdcan_stm32h5_stop(bsp_fdcan_stm32h5_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(!context->is_started)
    {
        return BSP_STATUS_OK;
    }
    if(HAL_FDCAN_Stop(&context->handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    context->is_started = false;
    return BSP_STATUS_OK;
}

/** @brief Perform an explicit FDCAN stop/start recovery outside interrupt context. */
bsp_status_t bsp_fdcan_stm32h5_recover(bsp_fdcan_stm32h5_context_t *context)
{
    if((context == NULL) || !context->is_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    ++context->diagnostics.recovery_attempts;
    if(context->is_started && (HAL_FDCAN_Stop(&context->handle) != HAL_OK))
    {
        ++context->diagnostics.recovery_failures;
        context->diagnostics.last_hal_error =
            HAL_FDCAN_GetError(&context->handle);
        return BSP_STATUS_IO_ERROR;
    }
    context->is_started = false;
    if(HAL_FDCAN_Start(&context->handle) != HAL_OK)
    {
        ++context->diagnostics.recovery_failures;
        context->diagnostics.last_hal_error =
            HAL_FDCAN_GetError(&context->handle);
        return BSP_STATUS_IO_ERROR;
    }
    context->is_started = true;
    return BSP_STATUS_OK;
}

/** @brief Queue one frame in the STM32H5 FDCAN transmit FIFO. */
bsp_status_t bsp_fdcan_stm32h5_send(bsp_fdcan_stm32h5_context_t *context,
                                    const bsp_fdcan_frame_t *frame)
{
    FDCAN_TxHeaderTypeDef header = {0};
    uint32_t dlc;
    bsp_status_t status;

    if((context == NULL) || (frame == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!context->is_started)
    {
        return BSP_STATUS_NOT_READY;
    }
    if((frame->identifier > (frame->extended_id ? 0x1FFFFFFFU : 0x7FFU)) ||
       (frame->remote_frame && frame->fd_format) ||
       (frame->fd_format && !context->fd_enabled) ||
       (frame->bitrate_switch && (!frame->fd_format ||
                                  !context->bitrate_switch_enabled)) ||
       (!frame->fd_format && (frame->length > 8U)))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    status = bsp_fdcan_length_to_dlc(frame->length, &dlc);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    if(HAL_FDCAN_GetTxFifoFreeLevel(&context->handle) == 0U)
    {
        return BSP_STATUS_BUSY;
    }

    header.Identifier = frame->identifier;
    header.IdType = frame->extended_id ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    header.TxFrameType = frame->remote_frame ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    header.DataLength = dlc;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = frame->bitrate_switch ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    header.FDFormat = frame->fd_format ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0U;
    if(HAL_FDCAN_AddMessageToTxFifoQ(&context->handle,
                                     &header,
                                     frame->data) != HAL_OK)
    {
        context->diagnostics.last_hal_error = HAL_FDCAN_GetError(&context->handle);
        return BSP_STATUS_IO_ERROR;
    }
    ++context->diagnostics.tx_frames;
    return BSP_STATUS_OK;
}

/** @brief Try to read one frame from STM32H5 receive FIFO 0. */
bsp_status_t bsp_fdcan_stm32h5_try_receive(bsp_fdcan_stm32h5_context_t *context,
                                           bsp_fdcan_frame_t *frame,
                                           bool *has_frame)
{
    FDCAN_RxHeaderTypeDef header = {0};

    if((context == NULL) || (frame == NULL) || (has_frame == NULL))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    *has_frame = false;
    if(!context->is_started)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(HAL_FDCAN_GetRxFifoFillLevel(&context->handle, FDCAN_RX_FIFO0) == 0U)
    {
        return BSP_STATUS_OK;
    }
    if(HAL_FDCAN_GetRxMessage(&context->handle,
                              FDCAN_RX_FIFO0,
                              &header,
                              frame->data) != HAL_OK)
    {
        context->diagnostics.last_hal_error = HAL_FDCAN_GetError(&context->handle);
        return BSP_STATUS_IO_ERROR;
    }
    frame->identifier = header.Identifier;
    frame->extended_id = header.IdType == FDCAN_EXTENDED_ID;
    frame->remote_frame = header.RxFrameType == FDCAN_REMOTE_FRAME;
    frame->fd_format = header.FDFormat == FDCAN_FD_CAN;
    frame->bitrate_switch = header.BitRateSwitch == FDCAN_BRS_ON;
    frame->length = bsp_fdcan_dlc_to_length(header.DataLength);
    *has_frame = true;
    ++context->diagnostics.rx_frames;
    return BSP_STATUS_OK;
}

/** @brief Dispatch one FDCAN interrupt to the STM32 HAL. */
void bsp_fdcan_stm32h5_irq(bsp_fdcan_stm32h5_context_t *context)
{
    if((context != NULL) && context->is_initialized)
    {
        HAL_FDCAN_IRQHandler(&context->handle);
    }
}

/** @brief Record FIFO activity and hardware overrun notifications. */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *handle,
                               uint32_t interrupt_flags)
{
    bsp_fdcan_stm32h5_context_t *context = bsp_fdcan_find_context(handle);

    if(context == NULL)
    {
        return;
    }
    if((interrupt_flags & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0U)
    {
        ++context->diagnostics.rx_events;
    }
    if((interrupt_flags & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0U)
    {
        ++context->diagnostics.rx_overruns;
    }
}

/** @brief Record FDCAN protocol-status transitions for diagnostics. */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *handle,
                                   uint32_t interrupt_flags)
{
    bsp_fdcan_stm32h5_context_t *context = bsp_fdcan_find_context(handle);

    if(context == NULL)
    {
        return;
    }
    ++context->diagnostics.error_events;
    if((interrupt_flags & FDCAN_IT_ERROR_WARNING) != 0U)
    {
        ++context->diagnostics.warning_events;
    }
    if((interrupt_flags & FDCAN_IT_ERROR_PASSIVE) != 0U)
    {
        ++context->diagnostics.passive_events;
    }
    if((interrupt_flags & FDCAN_IT_BUS_OFF) != 0U)
    {
        ++context->diagnostics.bus_off_events;
    }
    context->diagnostics.last_hal_error = HAL_FDCAN_GetError(handle);
}
