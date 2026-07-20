/**
 * @file test_param_store.c
 * @brief Host tests for atomic, journaled CHPM parameter persistence.
 */

#include "mock_flash.h"
#include "param.h"

#include <assert.h>
#include <stdio.h>

/** @brief Assert the documented factory parameter snapshot. */
static void assert_defaults(void)
{
    assert(g_tParam.Addr485 == 1U);
    assert(g_tParam.mode == 0U);
    assert(g_tParam.RestartCnt == 0U);
    assert(g_tParam.pwm_manual == 4000U);
    assert(g_tParam.pwm_auto == 4000U);
}

/** @brief Build and commit one complete test snapshot. */
static bool store(uint8_t addr,
                  uint8_t mode,
                  uint16_t restart_count,
                  uint16_t pwm_manual,
                  uint16_t pwm_auto)
{
    PARAM_T candidate;

    candidate.Addr485 = addr;
    candidate.mode = mode;
    candidate.RestartCnt = restart_count;
    candidate.pwm_manual = pwm_manual;
    candidate.pwm_auto = pwm_auto;
    return ParamCommit(&candidate) == PARAM_STORE_STATUS_OK;
}

/** @brief Exercise migration, rollback, duplicate suppression, and wear policy. */
int main(void)
{
    uint32_t before_calls;
    uint32_t before_reads;
    uint32_t before_erases;

    mock_flash_reset();
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_DEFAULTS);
    assert(ParamSequence() == 1U);
    assert_defaults();

    before_reads = mock_flash_read_calls();
    assert(store(17U, 1U, 9U, 6500U, 7200U));
    /* One verify read plus one final committed-record read; no sector rescan. */
    assert(mock_flash_read_calls() == before_reads + 2U);
    assert(ParamSequence() == 2U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_SLOT_A);
    assert(g_tParam.Addr485 == 17U && g_tParam.mode == 1U);
    assert(g_tParam.RestartCnt == 9U && g_tParam.pwm_manual == 6500U);

    /* Damaging the newest appended commit must fall back to the older record. */
    mock_flash_corrupt(PARAM_SLOT_A_ADDRESS + 64U + 33U, 0U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_SLOT_A);
    assert_defaults();

    /* A legacy v0x101 structure at address zero is migrated once. */
    mock_flash_reset();
    mock_flash_write_legacy(23U, 1U, 6000U, 7000U, 42U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_LEGACY);
    assert(g_tParam.Addr485 == 23U && g_tParam.RestartCnt == 42U);
    assert(ParamSequence() == 1U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_SLOT_A);

    /* A failed pre-commit write leaves the active runtime and slot unchanged. */
    before_calls = mock_flash_program_calls();
    mock_flash_fail_program_call(before_calls + 2U);
    assert(!store(24U, 0U, 43U, 6100U, 7100U));
    assert(g_tParam.Addr485 == 23U && g_tParam.RestartCnt == 42U);
    ParamLoad();
    assert(g_tParam.Addr485 == 23U && g_tParam.RestartCnt == 42U);

    before_calls = mock_flash_program_calls();
    assert(!store(0U, 0U, 0U, 4000U, 4000U));
    assert(mock_flash_program_calls() == before_calls);

    /* Re-submitting the durable snapshot must not consume another flash slot. */
    before_calls = mock_flash_program_calls();
    assert(store(23U, 1U, 42U, 6000U, 7000U));
    assert(mock_flash_program_calls() == before_calls);

    /*
     * Sixty-four records fit in one sector.  Frequent changes append records;
     * only sector rotation erases the inactive sector.
     */
    mock_flash_reset();
    ParamLoad();
    before_erases = mock_flash_erase_calls();
    for(uint16_t index = 0U; index < 70U; index++)
    {
        assert(store((uint8_t)(2U + index),
                     (uint8_t)(index & 1U),
                     index,
                     (uint16_t)(4000U + index),
                     (uint16_t)(5000U + index)));
    }
    assert(mock_flash_erase_calls() == before_erases);
    assert(ParamSpareNeedsErase());
    mock_flash_fail_erase_call(before_erases + 1U);
    assert(ParamPrepareSpare() == PARAM_STORE_STATUS_FLASH_ERROR);
    assert(ParamSpareNeedsErase());
    assert(mock_flash_erase_calls() == before_erases + 1U);
    assert(ParamPrepareSpare() == PARAM_STORE_STATUS_OK);
    assert(!ParamSpareNeedsErase());
    assert(mock_flash_erase_calls() == before_erases + 2U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_SLOT_B);
    assert(g_tParam.Addr485 == 71U && g_tParam.RestartCnt == 69U);

    /*
     * If both sectors become full before idle maintenance runs, the request
     * must report "spare not ready" without erasing in the commit path.
     */
    mock_flash_reset();
    ParamLoad();
    for(uint16_t index = 0U; index < 127U; index++)
    {
        PARAM_T candidate =
        {
            .Addr485 = (uint8_t)(2U + (index % 200U)),
            .mode = (uint8_t)(index & 1U),
            .RestartCnt = index,
            .pwm_manual = (uint16_t)(4000U + (index % 100U)),
            .pwm_auto = (uint16_t)(5000U + (index % 100U))
        };

        assert(ParamCommit(&candidate) == PARAM_STORE_STATUS_OK);
    }
    before_calls = mock_flash_program_calls();
    {
        PARAM_T candidate =
        {
            .Addr485 = 200U,
            .mode = 1U,
            .RestartCnt = 500U,
            .pwm_manual = 7000U,
            .pwm_auto = 8000U
        };

        assert(ParamCommit(&candidate) ==
               PARAM_STORE_STATUS_SPARE_NOT_READY);
        assert(mock_flash_program_calls() == before_calls);
        assert(ParamPrepareSpare() == PARAM_STORE_STATUS_OK);
        assert(ParamCommit(&candidate) == PARAM_STORE_STATUS_OK);
    }

    /* A power loss during sector rotation must retain the full old sector. */
    mock_flash_reset();
    ParamLoad();
    for(uint16_t index = 0U; index < 63U; index++)
    {
        assert(store((uint8_t)(2U + index), 0U, index,
                     (uint16_t)(4000U + index), 5000U));
    }
    before_calls = mock_flash_program_calls();
    mock_flash_fail_program_call(before_calls + 2U);
    assert(!store(100U, 1U, 100U, 8000U, 6000U));
    assert(g_tParam.Addr485 == 64U && g_tParam.RestartCnt == 62U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_SLOT_A);
    assert(g_tParam.Addr485 == 64U && g_tParam.RestartCnt == 62U);

    puts("CHPM parameter persistence tests passed");
    return 0;
}
