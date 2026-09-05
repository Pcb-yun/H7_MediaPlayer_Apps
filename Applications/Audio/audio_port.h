/**
 * @file audio_port.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 音频解码播放接口层码头文件
 */

#ifndef __AUDIO_PORT_H__
#define __AUDIO_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "audio_cfg.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 音频元数据
 */
typedef struct {
	uint32_t sample_rate;       // 采样率(Hz)
	uint8_t  channels;          // 声道数
	uint8_t  bits_per_sample;   // 位深(bit), MP3等有损格式为0
	uint64_t total_frames;      // 总PCM帧数
	uint32_t duration_ms;       // 总时长(ms)
} audio_meta_t;

/**
 * @brief 音频解码器接口
 */
typedef struct {
	/**
     * @brief  打开音频文件
     * @param  path 文件路径
     * @return 解码器句柄
     */
	void* (*open)(const uint8_t* path);

     /**
     * @brief  关闭解码器并释放资源
     * @param  dec 解码器句柄
     */
     void (*close)(void* dec);

     /**
     * @brief  读取PCM帧
     * @param  dec 解码器句柄
     * @param  buf  PCM数据缓冲区
     * @param  frames 读取帧数
     * @return 实际读取帧数
     */
	uint32_t (*read)(void* dec, float* buf, uint32_t frames);

	/**
     * @brief  跳转到指定PCM帧
     * @param  dec 解码器句柄
     * @param  frame 目标帧索引
     * @return 跳转状态
     */
	bool (*seek)(void* dec, uint32_t frame);


	/**
     * @brief 获取音频元数据
     * @param info 音频元数据结构体指针
     */
	void (*get_info)(void* dec, audio_meta_t* info);

} audio_decoder_t;








#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __AUDIO_PORT_H__ */
