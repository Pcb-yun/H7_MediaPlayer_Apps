/**
 * @file Boot.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 引导加载器头文件
 */

#ifndef __BOOT_H__
#define __BOOT_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>

void BootShared_Update(const uint8_t *path);





#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __BOOT_H__ */
