#include "mock_flash.h"
#include "param.h"

#include <assert.h>
#include <stdio.h>

static void assert_defaults(void)
{
    assert(g_tParam.Addr485 == 1U);
    assert(g_tParam.mode == 0U);
    assert(g_tParam.RestartCnt == 0U);
    assert(g_tParam.pwm_manual == 4000U);
    assert(g_tParam.pwm_auto == 4000U);
}

int main(void)
{
    uint32_t before_calls;

    mock_flash_reset();
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_DEFAULTS);
    assert(ParamSequence() == 1U);
    assert_defaults();

    assert(Param_Store(17U, 1U, 9U, 6500U, 7200U));
    assert(ParamSequence() == 2U);
    ParamLoad();
    assert(ParamLoadSource() == PARAM_LOAD_SLOT_B);
    assert(g_tParam.Addr485 == 17U && g_tParam.mode == 1U);
    assert(g_tParam.RestartCnt == 9U && g_tParam.pwm_manual == 6500U);

    /* Damaging the newest commit marker must fall back to the older slot. */
    mock_flash_corrupt(0x2000U + 33U, 0U);
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
    assert(!Param_Store(24U, 0U, 43U, 6100U, 7100U));
    assert(g_tParam.Addr485 == 23U && g_tParam.RestartCnt == 42U);
    ParamLoad();
    assert(g_tParam.Addr485 == 23U && g_tParam.RestartCnt == 42U);

    before_calls = mock_flash_program_calls();
    assert(!Param_Store(0U, 0U, 0U, 4000U, 4000U));
    assert(mock_flash_program_calls() == before_calls);

    puts("CHPM parameter persistence tests passed");
    return 0;
}
