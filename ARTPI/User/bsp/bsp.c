#include "bsp.h"
#include "tx_api.h"

static void system_clock_config(void);
static void mpu_config(void);
static void cpu_cache_enable(void);

void system_init(void)
{
#if defined(ART_PI_QSPI_APP)
    SCB->VTOR = BSP_QSPI_W25Q128_BASE_ADDRESS;
    __DSB();
    __ISB();
#endif
    mpu_config();
    cpu_cache_enable();

    if (bsp_dwt_init() != HAL_OK)
    {
        BSP_ERROR();
    }

    HAL_Init();
    system_clock_config();
}

void bsp_init(void)
{
    if (bsp_tim_init() != HAL_OK)
    {
        BSP_ERROR();
    }

    if (bsp_tim_pwm_init() != HAL_OK)
    {
        BSP_ERROR();
    }

    bsp_uart_init();
    if (bsp_rs485_init() != HAL_OK)
    {
        BSP_ERROR();
    }

    bsp_beep_init();
    bsp_led_init();
}

void bsp_delay_ms(uint32_t delay_ms)
{
    if (delay_ms == 0U)
    {
        return;
    }

    if ((__get_IPSR() == 0U) && (__get_PRIMASK() == 0U) &&
        (__get_BASEPRI() == 0U) && (__get_FAULTMASK() == 0U) &&
        (tx_thread_identify() != TX_NULL))
    {
        uint64_t ticks = ((uint64_t)delay_ms * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U;

        if (ticks == 0U)
        {
            ticks = 1U;
        }
        while (ticks > (uint64_t)(TX_WAIT_FOREVER - 1UL))
        {
            (void)tx_thread_sleep(TX_WAIT_FOREVER - 1UL);
            ticks -= (uint64_t)(TX_WAIT_FOREVER - 1UL);
        }
        (void)tx_thread_sleep((ULONG)ticks);
    }
    else
    {
        bsp_dwt_delay_ms(delay_ms);
    }
}

void bsp_delay_us(uint32_t delay_us)
{
    bsp_dwt_delay_us(delay_us);
}

void bsp_error_handler(const char *file, uint32_t line)
{
    (void)file;
    (void)line;

    __disable_irq();
    while (1)
    {
    }
}

static void system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator_config = {0};
    RCC_ClkInitTypeDef clock_config = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
    {
        BSP_ERROR();
    }

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    oscillator_config.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator_config.HSEState = RCC_HSE_ON;
    oscillator_config.HSIState = RCC_HSI_OFF;
    oscillator_config.CSIState = RCC_CSI_OFF;
    oscillator_config.PLL.PLLState = RCC_PLL_ON;
    oscillator_config.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator_config.PLL.PLLM = 5U;
    oscillator_config.PLL.PLLN = 192U;
    oscillator_config.PLL.PLLP = 2U;
    oscillator_config.PLL.PLLQ = 15U;
    oscillator_config.PLL.PLLR = 2U;
    oscillator_config.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    oscillator_config.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    oscillator_config.PLL.PLLFRACN = 0U;

    if (HAL_RCC_OscConfig(&oscillator_config) != HAL_OK)
    {
        BSP_ERROR();
    }

    clock_config.ClockType = RCC_CLOCKTYPE_HCLK |
                              RCC_CLOCKTYPE_SYSCLK |
                              RCC_CLOCKTYPE_PCLK1 |
                              RCC_CLOCKTYPE_PCLK2 |
                              RCC_CLOCKTYPE_D1PCLK1 |
                              RCC_CLOCKTYPE_D3PCLK1;
    clock_config.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock_config.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clock_config.AHBCLKDivider = RCC_HCLK_DIV2;
    clock_config.APB3CLKDivider = RCC_APB3_DIV2;
    clock_config.APB1CLKDivider = RCC_APB1_DIV2;
    clock_config.APB2CLKDivider = RCC_APB2_DIV2;
    clock_config.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_4) != HAL_OK)
    {
        BSP_ERROR();
    }

    __HAL_RCC_TIMCLKPRESCALER(RCC_TIMPRES_DESACTIVATED);

    __HAL_RCC_CSI_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_EnableCompensationCell();
}

static void mpu_config(void)
{
    MPU_Region_InitTypeDef region = {0};

    HAL_MPU_Disable();

    region.Enable = MPU_REGION_ENABLE;
    region.BaseAddress = 0x24000000U;
    region.Size = MPU_REGION_SIZE_512KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.SubRegionDisable = 0x00U;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&region);

    region.BaseAddress = 0x30040000U;
    region.Size = MPU_REGION_SIZE_32KB;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.Number = MPU_REGION_NUMBER1;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

#if defined(ART_PI_QSPI_APP)
    region.BaseAddress = BSP_QSPI_W25Q128_BASE_ADDRESS;
    region.Size = MPU_REGION_SIZE_8MB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.Number = MPU_REGION_NUMBER2;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.SubRegionDisable = 0x00U;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&region);
#endif

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void cpu_cache_enable(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}
