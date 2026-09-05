/**
 * @file get_res.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 返回值格式化工具头文件
 */

#ifndef __GET_RES_H
#define __GET_RES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>

#include "ff.h"
#include "fatfs.h"

const char* FATFS_GetString(FRESULT res);
const char* NameCheck_GetString(NameCheck_t res);









#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GET_RES_H */
