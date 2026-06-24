#include "bsp_pwm.h"

static bool bsp_pwm_valid(const bsp_pwm_t *pwm)
{
    return pwm && pwm->htim && pwm->htim->Instance;
}

bool bsp_pwm_start(const bsp_pwm_t *pwm)
{
    if(!bsp_pwm_valid(pwm))
        return false;

    return HAL_TIM_PWM_Start(pwm->htim, pwm->channel) == HAL_OK;
}

bool bsp_pwm_stop(const bsp_pwm_t *pwm)
{
    if(!bsp_pwm_valid(pwm))
        return false;

    return HAL_TIM_PWM_Stop(pwm->htim, pwm->channel) == HAL_OK;
}

bool bsp_pwm_set_duty_permille(const bsp_pwm_t *pwm, uint16_t duty_permille)
{
    uint32_t period;
    uint32_t pulse;

    if(!bsp_pwm_valid(pwm))
        return false;

    if(duty_permille > 1000U)
        duty_permille = 1000U;

    period = __HAL_TIM_GET_AUTORELOAD(pwm->htim);
    pulse = ((period + 1U) * (uint32_t)duty_permille) / 1000U;
    if(pulse > period)
        pulse = period;

    __HAL_TIM_SET_COMPARE(pwm->htim, pwm->channel, pulse);
    return true;
}

bool bsp_pwm_set_duty_percent(const bsp_pwm_t *pwm, uint8_t duty_percent)
{
    if(duty_percent > 100U)
        duty_percent = 100U;

    return bsp_pwm_set_duty_permille(pwm, (uint16_t)duty_percent * 10U);
}

bool bsp_pwm_set_frequency(const bsp_pwm_t *pwm, uint32_t timer_clock_hz, uint32_t frequency_hz)
{
    uint32_t period;

    if(!bsp_pwm_valid(pwm) || timer_clock_hz == 0U || frequency_hz == 0U)
        return false;

    period = (timer_clock_hz / frequency_hz);
    if(period == 0U)
        return false;

    period -= 1U;
    __HAL_TIM_SET_AUTORELOAD(pwm->htim, period);

    if(__HAL_TIM_GET_COMPARE(pwm->htim, pwm->channel) > period)
        __HAL_TIM_SET_COMPARE(pwm->htim, pwm->channel, period);

    return true;
}

uint32_t bsp_pwm_get_period(const bsp_pwm_t *pwm)
{
    if(!bsp_pwm_valid(pwm))
        return 0U;

    return __HAL_TIM_GET_AUTORELOAD(pwm->htim);
}

uint32_t bsp_pwm_get_pulse(const bsp_pwm_t *pwm)
{
    if(!bsp_pwm_valid(pwm))
        return 0U;

    return __HAL_TIM_GET_COMPARE(pwm->htim, pwm->channel);
}
