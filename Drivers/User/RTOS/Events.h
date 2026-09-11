/**
 * @file Events.h
 * @brief 系统事件定义
 * @author Pcb-yun (pcbyinyun@163.com)
 */

#ifndef __EVENTS_H__
#define __EVENTS_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "cmsis_os2.h"

/**
 * @note 每个事件标志对象最多可以设置24个成员
 */

/*                  系统状态                    */
extern osEventFlagsId_t System_StatusHandle;
#define SYS_INIT_COMPLETE (1UL << 0)    // 系统初始化完成
#define APP_NEED_USART (1UL << 1)       // 需要使用串口
#define SHELL_ONLINE (1UL << 2)         // shell在线
#define FS_MOUNTED (1UL << 3)           // 文件系统已挂载
#define USART1_REFRESH (1UL << 4)       // USART1缓存需要刷新


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __EVENTS_H__
