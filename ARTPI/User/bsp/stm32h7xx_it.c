/**
 * @file stm32h7xx_it.c
 * @brief ART-Pi H750 exception and peripheral interrupt handlers.
 */

#include "stm32h7xx_it.h"
#include "bsp_eth.h"
#include "bsp_sdio_wifi.h"

static void fault_uart_write(const char *text);

/** @brief Perform the NMI_Handler board-support operation. */
void NMI_Handler(void)
{
}

/** @brief Perform the HardFault_Handler board-support operation. */
void HardFault_Handler(void)
{
    fault_uart_write("\r\nHARD FAULT\r\n");
    while (1)
    {
    }
}

/** @brief Perform the MemManage_Handler board-support operation. */
void MemManage_Handler(void)
{
    fault_uart_write("\r\nMEMMANAGE FAULT\r\n");
    while (1)
    {
    }
}

/** @brief Perform the BusFault_Handler board-support operation. */
void BusFault_Handler(void)
{
    fault_uart_write("\r\nBUS FAULT\r\n");
    while (1)
    {
    }
}

/** @brief Perform the UsageFault_Handler board-support operation. */
void UsageFault_Handler(void)
{
    fault_uart_write("\r\nUSAGE FAULT\r\n");
    while (1)
    {
    }
}

/** @brief Perform the DebugMon_Handler board-support operation. */
void DebugMon_Handler(void)
{
}

/** @brief Handle the EXTI3_IRQHandler interrupt. */
void EXTI3_IRQHandler(void)
{
    bsp_sdio_wifi_oob_irq_handler();
}

/** @brief Handle the ETH_IRQHandler interrupt. */
void ETH_IRQHandler(void)
{
    bsp_eth_irq_handler();
}

/** @brief Perform the fault_uart_write board-support operation. */
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
