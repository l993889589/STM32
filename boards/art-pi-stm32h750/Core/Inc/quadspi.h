/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    quadspi.h
  * @brief   This file contains all the function prototypes for
  *          the quadspi.c file.
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __QUADSPI_H__
#define __QUADSPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern QSPI_HandleTypeDef hqspi;

void MX_QUADSPI_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __QUADSPI_H__ */
