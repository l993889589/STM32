/**
 * @file board_usb_device.c
 * @brief STM32H563 USB DRD full-speed device-controller binding.
 */

#include "bsp_usb_device.h"

#include <string.h>

#include "stm32h5xx_hal.h"

#define BOARD_USB_ENDPOINT_COUNT 8U
#define BOARD_USB_PMA_FIRST_ADDRESS 0x40U
#define BOARD_USB_PMA_END_ADDRESS USB_DRD_PMA_SIZE

static PCD_HandleTypeDef board_usb_handle;
static bsp_usb_device_handlers_t board_usb_handlers;
static bsp_usb_device_diagnostics_t board_usb_diagnostics;
static uint16_t board_usb_pma_address[2][BOARD_USB_ENDPOINT_COUNT];
static uint16_t board_usb_pma_size[2][BOARD_USB_ENDPOINT_COUNT];
static uint16_t board_usb_next_pma_address = BOARD_USB_PMA_FIRST_ADDRESS;
static bool board_usb_initialized;

/** @brief Validate a full-speed endpoint direction address. */
static bool board_usb_endpoint_address_is_valid(uint8_t endpoint_address)
{
    return ((endpoint_address & 0x70U) == 0U) &&
           ((endpoint_address & 0x0FU) < BOARD_USB_ENDPOINT_COUNT);
}

/** @brief Translate a portable endpoint type into the STM32 USB core domain. */
static bsp_status_t board_usb_map_endpoint_type(bsp_usb_endpoint_type_t type,
                                                uint8_t *hal_type)
{
    if(hal_type == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    switch(type)
    {
        case BSP_USB_ENDPOINT_CONTROL:
            *hal_type = EP_TYPE_CTRL;
            return BSP_STATUS_OK;
        case BSP_USB_ENDPOINT_ISOCHRONOUS:
            *hal_type = EP_TYPE_ISOC;
            return BSP_STATUS_OK;
        case BSP_USB_ENDPOINT_BULK:
            *hal_type = EP_TYPE_BULK;
            return BSP_STATUS_OK;
        case BSP_USB_ENDPOINT_INTERRUPT:
            *hal_type = EP_TYPE_INTR;
            return BSP_STATUS_OK;
        default:
            return BSP_STATUS_INVALID_ARGUMENT;
    }
}

/** @brief Validate endpoint-number and packet-size rules from USB 2.0 full speed. */
static bool board_usb_endpoint_configuration_is_valid(
    uint8_t endpoint_address,
    uint16_t maximum_packet_size,
    bsp_usb_endpoint_type_t type)
{
    uint8_t endpoint_number = endpoint_address & 0x0FU;

    if((endpoint_number == 0U) != (type == BSP_USB_ENDPOINT_CONTROL))
    {
        return false;
    }
    if(type == BSP_USB_ENDPOINT_CONTROL)
    {
        return (maximum_packet_size == 8U) ||
               (maximum_packet_size == 16U) ||
               (maximum_packet_size == 32U) ||
               (maximum_packet_size == 64U);
    }
    if(type == BSP_USB_ENDPOINT_ISOCHRONOUS)
    {
        return maximum_packet_size <= 1023U;
    }
    return maximum_packet_size <= 64U;
}

/** @brief Allocate one non-overlapping single-buffer region in USB packet RAM. */
static bsp_status_t board_usb_allocate_pma(uint8_t endpoint_address,
                                           uint16_t requested_size,
                                           uint16_t *pma_address)
{
    uint8_t direction = (endpoint_address & 0x80U) != 0U ? 1U : 0U;
    uint8_t endpoint_number = endpoint_address & 0x0FU;
    uint16_t aligned_size = (uint16_t)((requested_size + 1U) & 0xFFFEU);

    if(pma_address == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(board_usb_pma_address[direction][endpoint_number] != 0U)
    {
        if(requested_size > board_usb_pma_size[direction][endpoint_number])
        {
            return BSP_STATUS_NOT_SUPPORTED;
        }
        *pma_address = board_usb_pma_address[direction][endpoint_number];
        return BSP_STATUS_OK;
    }
    if(((uint32_t)board_usb_next_pma_address + aligned_size) >
       BOARD_USB_PMA_END_ADDRESS)
    {
        return BSP_STATUS_OVERFLOW;
    }
    *pma_address = board_usb_next_pma_address;
    board_usb_pma_address[direction][endpoint_number] =
        board_usb_next_pma_address;
    board_usb_pma_size[direction][endpoint_number] = requested_size;
    board_usb_next_pma_address =
        (uint16_t)(board_usb_next_pma_address + aligned_size);
    return BSP_STATUS_OK;
}

/** @brief Enable the HSI48 USB clock and its USB-SOF clock recovery. */
static bsp_status_t board_usb_configure_clock(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    RCC_CRSInitTypeDef clock_recovery = {0};

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    oscillator.HSI48State = RCC_HSI48_ON;
    if(HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }
    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_USB;
    peripheral_clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if(HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    __HAL_RCC_CRS_CLK_ENABLE();
    clock_recovery.Prescaler = RCC_CRS_SYNC_DIV1;
    clock_recovery.Source = RCC_CRS_SYNC_SOURCE_USB;
    clock_recovery.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    clock_recovery.ReloadValue =
        __HAL_RCC_CRS_RELOADVALUE_CALCULATE(HSI48_VALUE, 1000U);
    clock_recovery.ErrorLimitValue = 34U;
    clock_recovery.HSI48CalibrationValue = 32U;
    HAL_RCCEx_CRSConfig(&clock_recovery);

    HAL_PWREx_EnableVddUSB();
    __HAL_RCC_USB_CLK_ENABLE();
    return BSP_STATUS_OK;
}

/** @brief Initialize the USB DRD full-speed device controller. */
bsp_status_t bsp_usb_device_init(const bsp_usb_device_handlers_t *handlers)
{
    bsp_status_t status;

    if(board_usb_initialized)
    {
        return BSP_STATUS_ALREADY_INITIALIZED;
    }
    status = board_usb_configure_clock();
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    memset(&board_usb_handlers, 0, sizeof(board_usb_handlers));
    if(handlers != NULL)
    {
        board_usb_handlers = *handlers;
    }
    memset(&board_usb_diagnostics, 0, sizeof(board_usb_diagnostics));
    memset(&board_usb_pma_address, 0, sizeof(board_usb_pma_address));
    memset(&board_usb_pma_size, 0, sizeof(board_usb_pma_size));
    board_usb_next_pma_address = BOARD_USB_PMA_FIRST_ADDRESS;

    board_usb_handle.Instance = USB_DRD_FS;
    board_usb_handle.Init.dev_endpoints = BOARD_USB_ENDPOINT_COUNT;
    board_usb_handle.Init.speed = PCD_SPEED_FULL;
    board_usb_handle.Init.phy_itface = PCD_PHY_EMBEDDED;
    board_usb_handle.Init.Sof_enable =
        board_usb_handlers.start_of_frame != NULL ? ENABLE : DISABLE;
    board_usb_handle.Init.low_power_enable = DISABLE;
    board_usb_handle.Init.lpm_enable = DISABLE;
    board_usb_handle.Init.battery_charging_enable = DISABLE;
    board_usb_handle.Init.vbus_sensing_enable = DISABLE;
    board_usb_handle.Init.bulk_doublebuffer_enable = DISABLE;
    board_usb_handle.Init.iso_singlebuffer_enable = ENABLE;
    if(HAL_PCD_Init(&board_usb_handle) != HAL_OK)
    {
        return BSP_STATUS_IO_ERROR;
    }

    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 8U, 0U);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
    board_usb_initialized = true;
    return BSP_STATUS_OK;
}

/** @brief Start and connect the initialized USB controller. */
bsp_status_t bsp_usb_device_start(void)
{
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(board_usb_diagnostics.started)
    {
        return BSP_STATUS_OK;
    }
    if(HAL_PCD_Start(&board_usb_handle) != HAL_OK)
    {
        ++board_usb_diagnostics.errors;
        board_usb_diagnostics.last_hal_error = board_usb_handle.ErrorCode;
        return BSP_STATUS_IO_ERROR;
    }
    board_usb_diagnostics.started = true;
    return BSP_STATUS_OK;
}

/** @brief Stop and disconnect the initialized USB controller. */
bsp_status_t bsp_usb_device_stop(void)
{
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    if(!board_usb_diagnostics.started)
    {
        return BSP_STATUS_OK;
    }
    if(HAL_PCD_Stop(&board_usb_handle) != HAL_OK)
    {
        ++board_usb_diagnostics.errors;
        board_usb_diagnostics.last_hal_error = board_usb_handle.ErrorCode;
        return BSP_STATUS_IO_ERROR;
    }
    board_usb_diagnostics.started = false;
    board_usb_diagnostics.connected = false;
    return BSP_STATUS_OK;
}

/** @brief Program a host-assigned USB device address. */
bsp_status_t bsp_usb_device_set_address(uint8_t address)
{
    if(address > 127U)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_PCD_SetAddress(&board_usb_handle, address) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Allocate PMA and open one USB endpoint direction. */
bsp_status_t bsp_usb_device_endpoint_open(uint8_t endpoint_address,
                                          uint16_t maximum_packet_size,
                                          bsp_usb_endpoint_type_t type)
{
    uint8_t hal_type;
    uint16_t pma_address;
    bsp_status_t status;

    if(!board_usb_endpoint_address_is_valid(endpoint_address) ||
       (maximum_packet_size == 0U) ||
       !board_usb_endpoint_configuration_is_valid(endpoint_address,
                                                   maximum_packet_size,
                                                   type))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    status = board_usb_map_endpoint_type(type, &hal_type);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    status = board_usb_allocate_pma(endpoint_address,
                                    maximum_packet_size,
                                    &pma_address);
    if(status != BSP_STATUS_OK)
    {
        return status;
    }
    if((HAL_PCDEx_PMAConfig(&board_usb_handle,
                            endpoint_address,
                            PCD_SNG_BUF,
                            pma_address) != HAL_OK) ||
       (HAL_PCD_EP_Open(&board_usb_handle,
                        endpoint_address,
                        maximum_packet_size,
                        hal_type) != HAL_OK))
    {
        ++board_usb_diagnostics.errors;
        board_usb_diagnostics.last_hal_error = board_usb_handle.ErrorCode;
        return BSP_STATUS_IO_ERROR;
    }
    return BSP_STATUS_OK;
}

/** @brief Close one USB endpoint direction. */
bsp_status_t bsp_usb_device_endpoint_close(uint8_t endpoint_address)
{
    if(!board_usb_endpoint_address_is_valid(endpoint_address))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_PCD_EP_Close(&board_usb_handle, endpoint_address) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Start one non-blocking USB IN transfer. */
bsp_status_t bsp_usb_device_transmit(uint8_t endpoint_address,
                                     uint8_t *data,
                                     uint32_t length)
{
    if(!board_usb_endpoint_address_is_valid(endpoint_address) ||
       ((endpoint_address & 0x80U) == 0U) ||
       ((length > 0U) && (data == NULL)))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_diagnostics.started)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_PCD_EP_Transmit(&board_usb_handle,
                               endpoint_address,
                               data,
                               length) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Arm one non-blocking USB OUT transfer. */
bsp_status_t bsp_usb_device_receive(uint8_t endpoint_address,
                                    uint8_t *data,
                                    uint32_t capacity)
{
    if(!board_usb_endpoint_address_is_valid(endpoint_address) ||
       ((endpoint_address & 0x80U) != 0U) ||
       ((capacity > 0U) && (data == NULL)))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_diagnostics.started)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_PCD_EP_Receive(&board_usb_handle,
                              endpoint_address,
                              data,
                              capacity) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Set or clear one USB endpoint halt condition. */
bsp_status_t bsp_usb_device_endpoint_set_stall(uint8_t endpoint_address,
                                               bool is_stalled)
{
    HAL_StatusTypeDef hal_status;

    if(!board_usb_endpoint_address_is_valid(endpoint_address))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    hal_status = is_stalled ?
                 HAL_PCD_EP_SetStall(&board_usb_handle, endpoint_address) :
                 HAL_PCD_EP_ClrStall(&board_usb_handle, endpoint_address);
    return hal_status == HAL_OK ? BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Flush pending data for one USB endpoint direction. */
bsp_status_t bsp_usb_device_endpoint_flush(uint8_t endpoint_address)
{
    if(!board_usb_endpoint_address_is_valid(endpoint_address))
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    if(!board_usb_initialized)
    {
        return BSP_STATUS_NOT_READY;
    }
    return HAL_PCD_EP_Flush(&board_usb_handle, endpoint_address) == HAL_OK ?
           BSP_STATUS_OK : BSP_STATUS_IO_ERROR;
}

/** @brief Copy current USB controller diagnostics. */
bsp_status_t bsp_usb_device_get_diagnostics(
    bsp_usb_device_diagnostics_t *diagnostics)
{
    if(diagnostics == NULL)
    {
        return BSP_STATUS_INVALID_ARGUMENT;
    }
    *diagnostics = board_usb_diagnostics;
    return board_usb_initialized ? BSP_STATUS_OK : BSP_STATUS_NOT_READY;
}

/** @brief Dispatch the USB DRD FS interrupt. */
void USB_DRD_FS_IRQHandler(void)
{
    if(board_usb_initialized)
    {
        HAL_PCD_IRQHandler(&board_usb_handle);
    }
}

/** @brief Forward a received setup packet to the registered upper stack. */
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.setup_packets;
    if(board_usb_handlers.setup_received != NULL)
    {
        board_usb_handlers.setup_received((const uint8_t *)handle->Setup,
                                          board_usb_handlers.context);
    }
}

/** @brief Forward a completed OUT transfer to the registered upper stack. */
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *handle,
                                  uint8_t endpoint_number)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.out_completions;
    if(board_usb_handlers.out_complete != NULL)
    {
        board_usb_handlers.out_complete(
            endpoint_number,
            HAL_PCD_EP_GetRxCount(handle, endpoint_number),
            board_usb_handlers.context);
    }
}

/** @brief Forward a completed IN transfer to the registered upper stack. */
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *handle,
                                 uint8_t endpoint_number)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.in_completions;
    if(board_usb_handlers.in_complete != NULL)
    {
        board_usb_handlers.in_complete(endpoint_number,
                                       board_usb_handlers.context);
    }
}

/** @brief Forward a USB bus reset to the registered upper stack. */
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.resets;
    if(board_usb_handlers.bus_reset != NULL)
    {
        board_usb_handlers.bus_reset(board_usb_handlers.context);
    }
}

/** @brief Forward USB suspend state to the registered upper stack. */
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.suspends;
    board_usb_diagnostics.suspended = true;
    if(board_usb_handlers.suspend != NULL)
    {
        board_usb_handlers.suspend(board_usb_handlers.context);
    }
}

/** @brief Forward USB resume state to the registered upper stack. */
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.resumes;
    board_usb_diagnostics.suspended = false;
    if(board_usb_handlers.resume != NULL)
    {
        board_usb_handlers.resume(board_usb_handlers.context);
    }
}

/** @brief Forward USB connect state to the registered upper stack. */
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.connects;
    board_usb_diagnostics.connected = true;
    if(board_usb_handlers.connect != NULL)
    {
        board_usb_handlers.connect(board_usb_handlers.context);
    }
}

/** @brief Forward USB disconnect state to the registered upper stack. */
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.disconnects;
    board_usb_diagnostics.connected = false;
    if(board_usb_handlers.disconnect != NULL)
    {
        board_usb_handlers.disconnect(board_usb_handlers.context);
    }
}

/** @brief Forward USB start-of-frame events to the registered upper stack. */
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *handle)
{
    if(handle != &board_usb_handle)
    {
        return;
    }
    ++board_usb_diagnostics.start_of_frames;
    if(board_usb_handlers.start_of_frame != NULL)
    {
        board_usb_handlers.start_of_frame(board_usb_handlers.context);
    }
}
