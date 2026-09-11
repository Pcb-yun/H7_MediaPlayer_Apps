/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

/* USER CODE BEGIN Private defines */
#define USART1_RX_BUF_SIZE 1056
#define USART1_TX_BUF_SIZE 1056
#define USART1_STREAM_RX_BUF_SIZE 2048
#define USART1_STREAM_TX_BUF_SIZE 4096

/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */
void my_printf(const char *fmt, ...);
void my_print(const char *str, uint32_t len);
void USART1_Transmit_DMA(uint8_t *data, uint32_t len);

void usart1_stream_init(void);
uint16_t usart1_rx_stream_read(uint8_t *data, uint16_t max);
uint16_t usart1_rx_stream_write(const uint8_t *data, uint16_t size);
void usart1_rx_stream_reset(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

