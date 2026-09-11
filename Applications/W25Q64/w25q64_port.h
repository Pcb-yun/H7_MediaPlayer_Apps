/**
 * @file w25q64_port.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief W25Q64 接口层头文件
 */

#ifndef __W25Q64_PORT_H__
#define __W25Q64_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>

#define W25Q64_ONLY_MEMORY_MAPPED	1	// 仅使用内存映射


bool W25Q64_Init(void);





#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __W25Q64_PORT_H__ */
