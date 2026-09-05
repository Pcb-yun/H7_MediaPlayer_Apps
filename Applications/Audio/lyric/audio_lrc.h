/**
 * @file audio_lrc.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 歌词(lrc)解析组件头文件
 *        职责: 读取/解析.lrc文件并对外提供"当前时间对应的歌词行",
 *        供 ui/audio_tui.c 渲染歌词区。解析功能尚未实现, 暂为空壳。
 * @note  界面组件(ui)只负责渲染, 不直接接触lrc文件内容
 */

#ifndef __AUDIO_LRC_H__
#define __AUDIO_LRC_H__

#include "audio_port.h"
#if AUDIO_SUPPORT_LRC

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* 预留: 歌词行数据结构与解析接口将在实现时补充于此 */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* AUDIO_SUPPORT_LRC */

#endif /* __AUDIO_LRC_H__ */
