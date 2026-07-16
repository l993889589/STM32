/**
 * @file app_ldc_vnext_test.h
 * @brief ART-Pi UART4 hardware integration test for LDC vNext.
 */

#ifndef APP_LDC_VNEXT_TEST_H
#define APP_LDC_VNEXT_TEST_H

#include "stm32h7xx_hal.h"

/**
 * @brief Start the UART4 CRLF framing test task and bind UART4 RX interrupts.
 * @return HAL_OK on success, otherwise the BSP/ThreadX startup error.
 * @note Call once from ThreadX task context after bsp_init().
 */
HAL_StatusTypeDef app_ldc_vnext_test_start(void);

#endif
