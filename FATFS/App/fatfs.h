/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.h
  * @brief  Header for fatfs applications
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
#ifndef __fatfs_H
#define __fatfs_H
#ifdef __cplusplus
 extern "C" {
#endif

#include "ff.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h" /* defines SD_Driver as external */

/* USER CODE BEGIN Includes */
#include <stdbool.h>

/* USER CODE END Includes */

extern uint8_t retSD; /* Return value for SD */
extern char SDPath[4]; /* SD logical drive path */
extern FATFS SDFatFS; /* File system object for SD logical drive */
extern FIL SDFile; /* File object for SD */

void MX_FATFS_Init(void);

/* USER CODE BEGIN Prototypes */

/**
 * @brief 文件名检查结果枚举
 */
typedef enum {
  FIL_PASS = 0,         // 文件检查通过
  FIL_TYPE_ERROR,       // 文件类型不匹配错误
  FIL_NAME_NULL,        // 文件名为空错误
  FIL_TOO_LONG,         // 文件名过长错误
  FIL_EXTENSION_ERROR,  // 文件扩展名格式错误
  FIL_NAME_INVALID,     // 文件名包含非法字符
} NameCheck_t;


bool FS_Check(void);
size_t utf16to8(const TCHAR *utf16, uint8_t *utf8, size_t max_len);
size_t utf8to16(const uint8_t *utf8, TCHAR *utf16, size_t max_len);
NameCheck_t FileNameCheck(const uint8_t *FileName, bool CaseSensitive, ...);
FRESULT F_open(FIL **file, const uint8_t *path, BYTE mode);
FRESULT F_close(FIL **file);


/* USER CODE END Prototypes */
#ifdef __cplusplus
}
#endif
#endif /*__fatfs_H */
