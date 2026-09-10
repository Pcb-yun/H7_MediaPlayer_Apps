/**
 * @file audio_tui.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 播放器界面绘制组件头文件
 */

#ifndef __AUDIO_TUI_H__
#define __AUDIO_TUI_H__

#include "audio_port.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


void tui_init(const audio_meta_t* meta, uint8_t volume);
void tui_update(const audio_time_t* time, uint8_t volume);
void tui_clear(void);






#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __AUDIO_TUI_H__ */
