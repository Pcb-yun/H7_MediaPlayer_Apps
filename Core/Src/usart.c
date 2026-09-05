/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
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
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include "string.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "Events.h"
#include "log.h"

static uint8_t usart1_rx_buf[USART1_RX_BUF_SIZE] __attribute__((section(".RAM_D2")));
static uint8_t usart1_tx_chunk_buf[USART1_TX_BUF_SIZE] __attribute__((section(".RAM_D2")));

static StreamBufferHandle_t usart1_rx_stream = NULL;
static StreamBufferHandle_t usart1_tx_stream = NULL;

/* USER CODE END 0 */

UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 1250000;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */
  HAL_UARTEx_ReceiveToIdle_DMA(&huart1, usart1_rx_buf, USART1_RX_BUF_SIZE);
  usart1_stream_init();

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16910CLKSOURCE_D2PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA1_Stream4;
    hdma_usart1_rx.Init.Request = DMA_REQUEST_USART1_RX;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    /* USART1_TX Init */
    hdma_usart1_tx.Instance = DMA1_Stream5;
    hdma_usart1_tx.Init.Request = DMA_REQUEST_USART1_TX;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart1_tx);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

    /* USART1 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/**
 * @brief  创建串口收发流缓冲
 */
void usart1_stream_init(void) {
  if (usart1_rx_stream == NULL) {
    usart1_rx_stream = xStreamBufferCreate(USART1_RX_BUF_SIZE, 1);
  }
  if (usart1_tx_stream == NULL) {
    usart1_tx_stream = xStreamBufferCreate(USART1_TX_BUF_SIZE, 1);
  }
}

/**
 * @brief  环形缓冲批量读取
 * @param  data 数据缓冲区
 * @param  max 期望读取长度
 * @return 实际读取长度
 */
__attribute__((section(".ITCM")))
uint16_t usart1_rx_stream_read(uint8_t *data, uint16_t max) {
  return xStreamBufferReceive(usart1_rx_stream, data, max, 0);
}

/**
 * @brief  环形缓冲写入
 * @param  data 数据
 * @param  size 数据长度
 * @return 实际写入长度
 */
__attribute__((section(".ITCM")))
uint16_t usart1_rx_stream_write(const uint8_t *data, uint16_t size) {
  return (uint16_t)xStreamBufferSend(usart1_rx_stream, data, size, 0);
}

/**
 * @brief  环形缓冲清空
 */
void usart1_rx_stream_reset(void) {
  xStreamBufferReset(usart1_rx_stream);
}

/**
 * @brief 串口接收事件回调函数
 * @param huart 串口句柄
 * @param Size 接收数据长度
 */
__attribute__((section(".ITCM")))
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART1) {
    if (huart->RxEventType == HAL_UART_RXEVENT_IDLE) {
      size_t actual_size = xStreamBufferSendFromISR(usart1_rx_stream, usart1_rx_buf, Size, NULL);
      HAL_UARTEx_ReceiveToIdle_DMA(&huart1, usart1_rx_buf, USART1_RX_BUF_SIZE);
    }
  }
}

/**
 * @brief 串口错误回调函数
 * @param huart 串口句柄
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {

  if (huart->Instance == USART1) {
		__HAL_UART_CLEAR_FLAG(huart, UART_FLAG_PE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_ORE);

		huart->RxState = HAL_UART_STATE_READY;
		huart->gState = HAL_UART_STATE_READY;

		HAL_UARTEx_ReceiveToIdle_DMA(huart, usart1_rx_buf, USART1_RX_BUF_SIZE);
  }
}

/**
 * @brief  判断USART1是否正在发送
 * @return true 发送中，false 空闲
 */
__attribute__((section(".ITCM")))
static bool usart1_tx_is_busy(void) {
	return !__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC);
}

/**
 * @brief USART1发送统一入口
 * @param data 待发送数据指针
 * @param len 待发送数据长度
 */
__attribute__((section(".ITCM")))
void USART1_Transmit_DMA(uint8_t *data, uint32_t len) {
	if (data == NULL || len == 0) {
		return;
	}
	xStreamBufferSend(usart1_tx_stream, data, len, portMAX_DELAY);

	if (!usart1_tx_is_busy()) {
		size_t n = xStreamBufferReceive(usart1_tx_stream, usart1_tx_chunk_buf, USART1_TX_BUF_SIZE, 0);
		if (n != 0) {
			HAL_UART_Transmit_DMA(&huart1, usart1_tx_chunk_buf, (uint16_t)n);
		}
	}
}

/**
 * @brief 串口发送完成回调函数
 * @param huart 串口句柄
 */
__attribute__((section(".ITCM")))
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART1) {
		size_t len = xStreamBufferReceiveFromISR(usart1_tx_stream, usart1_tx_chunk_buf, USART1_TX_BUF_SIZE, NULL);
		if (len != 0) {
			HAL_UART_Transmit_DMA(&huart1, usart1_tx_chunk_buf, (uint16_t)len);
		}
	}
}

/**
 * @brief 自定义printf函数
 *
 * @note 此函数为保险用，为了确保输出的原子性，会使用阻塞方式等待UART1完成发送。
 *       一般输出建议使用logPrintln函数。
 *
 * @param fmt 格式字符串
 * @param ... 可变参数
 */
void my_printf(const char *fmt, ...) {
  char buffer[1024];
  va_list ap;
  int len, actual_len;

  va_start(ap, fmt);
  len = vsnprintf(buffer, 1023, fmt, ap);
  va_end(ap);

  actual_len = (len > 1023) ? (1023) : len;

  HAL_UART_AbortTransmit(&huart1);
  HAL_UART_Transmit(&huart1, (uint8_t *)(buffer), actual_len, 0xFFFF);
}

/**
 * @brief 自定义print函数
 *
 * @note 此函数同样为保险用，使用完全阻塞方式发送。
 *
 * @param str 字符串指针
 * @param len 字符串长度
 */
void my_print(const char *str, uint32_t len) {
	const uint16_t chunk_size = 0xFFFF;
	uint32_t offset = 0;

	HAL_UART_AbortTransmit(&huart1);

	while (offset < len) {
		uint16_t send_len = (len - offset > chunk_size) ? chunk_size : (uint16_t)(len - offset);
		HAL_UART_Transmit(&huart1, (uint8_t *)(str + offset), send_len, 0xFFFFFF);
		while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC) == RESET);
		offset += send_len;
	}
}

/* USER CODE END 1 */

