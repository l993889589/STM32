#include "stm32h7xx_it.h"
#include "bsp_sdio_wifi.h"

static void fault_uart_write(const char *text);

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    fault_uart_write("\r\nHARD FAULT during AP6212 bring-up\r\n");
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    fault_uart_write("\r\nMEMMANAGE FAULT during AP6212 bring-up\r\n");
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    fault_uart_write("\r\nBUS FAULT during AP6212 bring-up\r\n");
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    fault_uart_write("\r\nUSAGE FAULT during AP6212 bring-up\r\n");
    while (1)
    {
    }
}

void DebugMon_Handler(void)
{
}

void EXTI3_IRQHandler(void)
{
    bsp_sdio_wifi_oob_irq_handler();
}

static void fault_uart_write(const char *text)
{
    while (*text != '\0')
    {
        while ((UART4->ISR & USART_ISR_TXE_TXFNF) == 0U)
        {
        }
        UART4->TDR = (uint8_t)*text;
        text++;
    }
}
