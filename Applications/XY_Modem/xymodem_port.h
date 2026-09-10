/**
 * @file xymodem_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief XYmodem协议用户接口头文件
 */

#ifndef __XYMODEM_PORT_H__
#define __XYMODEM_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "xymodem.h"

#define XYMODEM_USE_XMODEM 0    // 启用 Xmodem 协议
#define XYMODEM_USE_YMODEM 1    // 启用 Ymodem 协议

#define XYMODEM_RESERVED_MEM (2 * 1024)		// 为系统运行保留的内存







#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __XYMODEM_PORT_H__ */
