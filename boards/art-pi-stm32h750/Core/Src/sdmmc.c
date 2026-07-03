/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdmmc.c
  * @brief   SDMMC2 low-level bus setup for AP6212 SDIO probing.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "sdmmc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void MX_SDMMC2_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
  PeriphClkInitStruct.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_SDMMC2_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF9_SDMMC2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Alternate = GPIO_AF11_SDMMC2;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Alternate = GPIO_AF11_SDMMC2;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  SDMMC2->POWER = 0U;
  SDMMC2->CLKCR = 0U;
  SDMMC2->DCTRL = 0U;
  SDMMC2->ICR = 0x1DC007FFU;

  SDMMC2->POWER |= SDMMC_POWER_PWRCTRL;
  SDMMC2->CLKCR = (0xFAU & SDMMC_CLKCR_CLKDIV);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
