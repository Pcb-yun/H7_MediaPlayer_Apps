/**
 * @file audio_wav.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief wav格式音频解码头文件
 */

#ifndef __AUDIO_WAV_H__
#define __AUDIO_WAV_H__

#include "audio_port.h"
#if AUDIO_SUPPORT_WAV

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern const audio_decoder_t audio_wav_decoder;

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* AUDIO_SUPPORT_WAV */

#endif /* __AUDIO_WAV_H__ */
