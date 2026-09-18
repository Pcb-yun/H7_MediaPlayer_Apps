/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : mdma.c
  * Description        : This file provides code for the configuration
  *                      of all the requested global MDMA transfers.
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
#include "mdma.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure MDMA                                                              */
/*----------------------------------------------------------------------------*/

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
MDMA_HandleTypeDef hmdma_mdma_channel10_sw_0;

/**
  * Enable MDMA controller clock
  * Configure MDMA for global transfers
  *   hmdma_mdma_channel10_sw_0
  */
void MX_MDMA_Init(void)
{

  /* MDMA controller clock enable */
  __HAL_RCC_MDMA_CLK_ENABLE();
  /* Local variables */

  /* Configure MDMA channel MDMA_Channel10 */
  /* Configure MDMA request hmdma_mdma_channel10_sw_0 on MDMA_Channel10 */
  hmdma_mdma_channel10_sw_0.Instance = MDMA_Channel10;
  hmdma_mdma_channel10_sw_0.Init.Request = MDMA_REQUEST_SW;
  hmdma_mdma_channel10_sw_0.Init.TransferTriggerMode = MDMA_FULL_TRANSFER;
  hmdma_mdma_channel10_sw_0.Init.Priority = MDMA_PRIORITY_HIGH;
  hmdma_mdma_channel10_sw_0.Init.Endianness = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  hmdma_mdma_channel10_sw_0.Init.SourceInc = MDMA_SRC_INC_HALFWORD;
  hmdma_mdma_channel10_sw_0.Init.DestinationInc = MDMA_DEST_INC_HALFWORD;
  hmdma_mdma_channel10_sw_0.Init.SourceDataSize = MDMA_SRC_DATASIZE_HALFWORD;
  hmdma_mdma_channel10_sw_0.Init.DestDataSize = MDMA_DEST_DATASIZE_HALFWORD;
  hmdma_mdma_channel10_sw_0.Init.DataAlignment = MDMA_DATAALIGN_RIGHT;
  hmdma_mdma_channel10_sw_0.Init.BufferTransferLength = 128;
  hmdma_mdma_channel10_sw_0.Init.SourceBurst = MDMA_SOURCE_BURST_8BEATS;
  hmdma_mdma_channel10_sw_0.Init.DestBurst = MDMA_DEST_BURST_8BEATS;
  hmdma_mdma_channel10_sw_0.Init.SourceBlockAddressOffset = 0;
  hmdma_mdma_channel10_sw_0.Init.DestBlockAddressOffset = 0;
  if (HAL_MDMA_Init(&hmdma_mdma_channel10_sw_0) != HAL_OK)
  {
    Error_Handler();
  }

  /* MDMA interrupt initialization */
  /* MDMA_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(MDMA_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(MDMA_IRQn);

}
/* USER CODE BEGIN 2 */

/* USER CODE END 2 */

/**
  * @}
  */

/**
  * @}
  */

