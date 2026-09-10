/**
 * @file audio_lrc.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 歌词组件头文件
 */

#ifndef __AUDIO_LRC_H__
#define __AUDIO_LRC_H__

#include "audio_port.h"
#if AUDIO_SUPPORT_LRC

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief 歌词数据存储
 */
typedef struct {
    uint8_t *lrc_data;  // 歌词数据缓冲区指针
    uint32_t curent_ptr; // 当前读指针
    uint32_t size;      // 歌词数据大小
} lrcdata_t;

/**
 * @brief 行歌词
 */
typedef struct {
    audio_time_t time;  // 开始时间
    uint8_t *raw;       // 原词(以\0终止)
    uint8_t *tras;      // 翻译(以\0终止, 无翻译时为NULL)
} lrc_t;


void Lrc_register(uint8_t *data, uint32_t size);
const lrc_t *Lrc_getlen(audio_time_t time);
void Lrc_free(void);







#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* AUDIO_SUPPORT_LRC */

#endif /* __AUDIO_LRC_H__ */
