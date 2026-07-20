/**
 * @file mock_flash.h
 * @brief Host-side NOR flash controls for parameter persistence tests.
 */

#ifndef TEST_MOCK_FLASH_H
#define TEST_MOCK_FLASH_H

#include <stdint.h>

/** @brief Restore the complete mock flash to its erased state. */
void mock_flash_reset(void);

/** @brief Fail one absolute program-call number. */
void mock_flash_fail_program_call(uint32_t call_index);

/** @brief Fail one absolute sector-erase call number. */
void mock_flash_fail_erase_call(uint32_t call_index);

/** @brief Overwrite one mock byte to emulate stored-data corruption. */
void mock_flash_corrupt(uint32_t address, uint8_t value);

/** @brief Install one valid legacy v0x101 parameter image. */
void mock_flash_write_legacy(uint8_t address, uint8_t mode,
                             uint16_t manual_pwm, uint16_t auto_pwm,
                             uint32_t restart_count);

/** @brief Return the number of mock page-program operations. */
uint32_t mock_flash_program_calls(void);

/** @brief Return the number of mock read operations. */
uint32_t mock_flash_read_calls(void);

/** @brief Return the number of mock sector-erase operations. */
uint32_t mock_flash_erase_calls(void);

#endif /* TEST_MOCK_FLASH_H */
