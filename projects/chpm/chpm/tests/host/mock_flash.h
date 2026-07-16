#ifndef TEST_MOCK_FLASH_H
#define TEST_MOCK_FLASH_H

#include <stdint.h>

void mock_flash_reset(void);
void mock_flash_fail_program_call(uint32_t call_index);
void mock_flash_corrupt(uint32_t address, uint8_t value);
void mock_flash_write_legacy(uint8_t address, uint8_t mode,
                             uint16_t manual_pwm, uint16_t auto_pwm,
                             uint32_t restart_count);
uint32_t mock_flash_program_calls(void);

#endif
