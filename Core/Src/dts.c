/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dts.c
  * @brief   This file provides code for the configuration
  *          of the DTS instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "dts.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

DTS_HandleTypeDef hdts;

/* DTS init function */
void MX_DTS_Init(void)
{

  /* USER CODE BEGIN DTS_Init 0 */

  /* USER CODE END DTS_Init 0 */

  /* USER CODE BEGIN DTS_Init 1 */

  /* USER CODE END DTS_Init 1 */
  hdts.Instance = DTS;
  hdts.Init.QuickMeasure = DTS_QUICKMEAS_DISABLE;
  hdts.Init.RefClock = DTS_REFCLKSEL_PCLK;
  hdts.Init.TriggerInput = DTS_TRIGGER_HW_NONE;
  hdts.Init.SamplingTime = DTS_SMP_TIME_15_CYCLE;
  hdts.Init.Divider = 1;
  hdts.Init.HighThreshold = 0x0;
  hdts.Init.LowThreshold = 0x0;
  if (HAL_DTS_Init(&hdts) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DTS_Init 2 */

  /* USER CODE END DTS_Init 2 */

}

void HAL_DTS_MspInit(DTS_HandleTypeDef* dtsHandle)
{

  if(dtsHandle->Instance==DTS)
  {
  /* USER CODE BEGIN DTS_MspInit 0 */

  /* USER CODE END DTS_MspInit 0 */
    /* DTS clock enable */
    __HAL_RCC_DTS_CLK_ENABLE();
  /* USER CODE BEGIN DTS_MspInit 1 */

  /* USER CODE END DTS_MspInit 1 */
  }
}

void HAL_DTS_MspDeInit(DTS_HandleTypeDef* dtsHandle)
{

  if(dtsHandle->Instance==DTS)
  {
  /* USER CODE BEGIN DTS_MspDeInit 0 */

  /* USER CODE END DTS_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_DTS_CLK_DISABLE();
  /* USER CODE BEGIN DTS_MspDeInit 1 */

  /* USER CODE END DTS_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

