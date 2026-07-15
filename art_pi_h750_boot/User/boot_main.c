#include "stm32h7xx_hal.h"
#include "bsp_qspi_w25q128.h"
#include "boot_uart.h"
#include "boot_control.h"

#define BOOT_APP_BASE_ADDRESS   BSP_QSPI_W25Q128_BASE_ADDRESS
#define BOOT_SRAM_START         0x20000000UL
#define BOOT_SRAM_END           0x20080000UL

typedef void (*boot_entry_t)(void);

static void boot_system_clock_config(void);
static uint8_t boot_app_vector_valid(uint32_t stack_pointer,
                                     uint32_t reset_handler);
static void boot_jump_to_app(void);
static void boot_error(void);

int main(void)
{
    boot_control_status_t control_status;

    HAL_Init();
    boot_system_clock_config();
    (void)boot_uart_init();
    boot_uart_write("\r\nART-Pi H750 boot: start\r\n");

    if(bsp_qspi_w25q128_init() != HAL_OK)
    {
        boot_uart_write("ART-Pi H750 boot: QSPI init failed\r\n");
        boot_error();
    }
    control_status = boot_control_prepare();
    if(control_status == BOOT_CONTROL_ERROR)
    {
        boot_uart_write("ART-Pi H750 boot: A/B control failed\r\n");
        boot_error();
    }
    boot_uart_write((control_status == BOOT_CONTROL_NO_RECORD) ?
                    "ART-Pi H750 boot: factory image not enrolled\r\n" :
                    "ART-Pi H750 boot: A/B control ready\r\n");
    if(bsp_qspi_w25q128_enter_memory_mapped() != HAL_OK)
    {
        boot_uart_write("ART-Pi H750 boot: QSPI map failed\r\n");
        boot_error();
    }

    boot_jump_to_app();
    boot_error();
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

static void boot_system_clock_config(void)
{
    RCC_OscInitTypeDef oscillator_config = {0};
    RCC_ClkInitTypeDef clock_config = {0};

    if(HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
    {
        boot_error();
    }
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
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
    if(HAL_RCC_OscConfig(&oscillator_config) != HAL_OK)
    {
        boot_error();
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
    if(HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_4) != HAL_OK)
    {
        boot_error();
    }
}

static uint8_t boot_app_vector_valid(uint32_t stack_pointer,
                                     uint32_t reset_handler)
{
    if(stack_pointer < BOOT_SRAM_START || stack_pointer > BOOT_SRAM_END ||
       (stack_pointer & 0x7U) != 0U)
    {
        return 0U;
    }
    if(reset_handler < BOOT_APP_BASE_ADDRESS ||
       reset_handler >= (BOOT_APP_BASE_ADDRESS + 0x00200000UL) ||
       (reset_handler & 1U) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static void boot_jump_to_app(void)
{
    uint32_t stack_pointer = *(volatile uint32_t *)BOOT_APP_BASE_ADDRESS;
    uint32_t reset_handler = *(volatile uint32_t *)(BOOT_APP_BASE_ADDRESS + 4U);
    boot_entry_t entry;

    if(boot_app_vector_valid(stack_pointer, reset_handler) == 0U)
    {
        boot_uart_write("ART-Pi H750 boot: invalid app vector\r\n");
        return;
    }

    boot_uart_write("ART-Pi H750 boot: jump 0x90000000\r\n");
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR = BOOT_APP_BASE_ADDRESS;
    __set_MSP(stack_pointer);
    __DSB();
    __ISB();
    entry = (boot_entry_t)reset_handler;
    entry();
}

static void boot_error(void)
{
    __disable_irq();
    while(1)
    {
    }
}
