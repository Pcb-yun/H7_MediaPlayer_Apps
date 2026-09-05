/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */
#include "Events.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdarg.h>

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */

  if(retSD != 0) return;

  FRESULT res = f_mount(&SDFatFS, (const TCHAR*)SDPath, 1);
  if(res != FR_OK) return;
  osEventFlagsSet(System_StatusHandle, FS_MOUNTED);
  /* USER CODE END Init */
}

/* USER CODE BEGIN Application */

/**
 * @brief 检查文件系统是否挂载
 * @return true:已挂载 false:未挂载
 */
bool FS_Check(void) {
    return (osEventFlagsGet(System_StatusHandle) & FS_MOUNTED) ? true : false;
}

/**
 * @brief 打开文件
 * @param file 文件对象指针
 * @param path 文件路径
 * @param mode 打开模式
 * @return 打开结果
 */
FRESULT F_open(FIL **file, const uint8_t *path, BYTE mode) {
	FRESULT res;
	TCHAR wfile[512];

	if (file == NULL || path == NULL) return FR_INVALID_PARAMETER;

	*file = pvPortMalloc(sizeof(FIL));
	if (*file == NULL) return FR_NOT_ENOUGH_CORE;

	utf8to16(path, wfile, sizeof(wfile) / sizeof(TCHAR));
	res = f_open(*file, wfile, mode);
	if (res != FR_OK) {
		vPortFree(*file);
	}
	return res;
}

/**
 * @brief 关闭文件
 * @param file 文件对象指针
 * @return 关闭结果
 */
FRESULT F_close(FIL **file) {
	if (file == NULL || *file == NULL) return FR_INVALID_PARAMETER;

	FRESULT res = f_close(*file);
	vPortFree(*file);
	return res;
}

/**
 * @brief UTF-16到UTF-8转换函数
 * @param utf16 UTF-16字符串
 * @param utf8 UTF-8字符串缓冲区
 * @param max_len UTF-8缓冲区最大长度（字符数）
 * @return 转换后的UTF-8字符串长度（字符数）
 */
size_t utf16to8(const TCHAR *utf16, uint8_t *utf8, size_t max_len) {
    size_t j = 0;
    const WCHAR *wptr = (const WCHAR*)utf16;
    uint8_t *cptr = utf8;
    size_t remaining = max_len;

    while (*wptr && remaining > 1) {
        WCHAR wch = *wptr++;

        if (wch < 0x80) {
            if (remaining < 2) break;
            *cptr++ = (uint8_t)wch;
            remaining--; j++;
        } else if (wch < 0x800) {
            if (remaining < 3) break;
            *cptr++ = (uint8_t)(0xC0 | (wch >> 6));
            *cptr++ = (uint8_t)(0x80 | (wch & 0x3F));
            remaining -= 2; j += 2;
       } else {
            if (remaining < 4) break;
            *cptr++ = (uint8_t)(0xE0 | (wch >> 12));
            *cptr++ = (uint8_t)(0x80 | ((wch >> 6) & 0x3F));
            *cptr++ = (uint8_t)(0x80 | (wch & 0x3F));
            remaining -= 3; j += 3;
        }
    }

    if (remaining > 0) *cptr = '\0';
    else utf8[max_len - 1] = '\0';

    return j;
}

/**
 * @brief UTF-8到UTF-16转换函数
 * @param utf8 UTF-8字符串
 * @param utf16 UTF-16字符串缓冲区
 * @param max_len UTF-16缓冲区最大长度（字符数）
 * @return 转换后的UTF-16字符串长度（字符数）
 */
size_t utf8to16(const uint8_t *utf8, TCHAR *utf16, size_t max_len) {
    size_t j = 0;

    while (*utf8 && j < max_len - 1) {
        WCHAR wch;

        if ((*utf8 & 0x80) == 0) {
            utf16[j++] = *utf8++;
        } else if ((*utf8 & 0xE0) == 0xC0) {
            if (j >= max_len - 1) break;
            wch = ((utf8[0] & 0x1F) << 6) | (utf8[1] & 0x3F);
            utf16[j++] = wch; utf8 += 2;
        } else if ((*utf8 & 0xF0) == 0xE0) {
            if (j >= max_len - 1) break;
            wch = ((utf8[0] & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
            utf16[j++] = wch; utf8 += 3;
        } else {
            utf8++;
        }
    }

    utf16[j] = 0;
    return j;
}

/**
 * @brief 检查文件名是否符合指定的文件类型
 * @param FileName 要检查的文件名
 * @param CaseSensitive 是否大小写敏感
 * @param ... 可变参数，期望的文件类型（扩展名），以NULL结尾
 * @return 检查结果
 */
NameCheck_t FileNameCheck(const uint8_t *FileName, bool CaseSensitive, ...) {
  if (FileName == NULL) return FIL_NAME_NULL;

  size_t FileNameLen = strlen((char *)FileName);
  if (FileNameLen == 0) return FIL_NAME_NULL;

  // 将UTF-8转换为UTF-16，计算实际字符数
  TCHAR utf16_filename[_MAX_LFN + 1];
  UINT utf16_len = utf8to16(FileName, utf16_filename, _MAX_LFN + 1);

  // 检查文件名长度是否超过FATFS支持的最大长度
  if (utf16_len > _MAX_LFN) return FIL_TOO_LONG;

  // 检查文件名是否包含非法字符
  const char *invalid_chars = "<>:/\\|?*\"";
  for (size_t i = 0; i < FileNameLen; i++) {
    if (strchr(invalid_chars, FileName[i]) != NULL)
      return FIL_NAME_INVALID;
  }

  // 使用可变参数检查文件名是否以任一扩展名结尾
  va_list args;
  va_start(args, CaseSensitive);

  char *FileType;
  bool match_found = false;

  // 遍历所有传入的扩展名
  while ((FileType = va_arg(args, char *)) != NULL) {
    if (FileType == NULL) continue; // 跳过无效的扩展名
    size_t FileTypeLen = strlen(FileType);

    // 检查文件名长度是否大于等于当前文件类型长度
    if (FileNameLen < FileTypeLen) break;

    // 检查文件名是否以当前FileType结尾
    if (CaseSensitive) {
      if (strcmp((char *)FileName + FileNameLen - FileTypeLen, FileType) == 0)
        match_found = true; break; // 大小写敏感
    } else {
      if (strcasecmp((char *)FileName + FileNameLen - FileTypeLen, FileType) == 0)
        match_found = true; break; // 大小写不敏感
    }
  }

  va_end(args);

  if (!match_found) return FIL_TYPE_ERROR;
  return FIL_PASS;
}

/* USER CODE END Application */
