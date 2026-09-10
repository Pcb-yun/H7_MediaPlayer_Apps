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
#include "audio_res.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


/**
 * @brief 音频元数据
 */
typedef struct {
    uint32_t sample_rate;       // 采样率(Hz)
    uint8_t  channels;          // 声道数
    uint8_t  bits_per_sample;   // 位深(bit), MP3等有损格式为0
    uint64_t total_frames;      // 总PCM帧数
    uint32_t duration_ms;       // 总时长(ms)
    char title[AUDIO_META_TAG_LEN];     // 歌名(无标签为空串)
    char artist[AUDIO_META_TAG_LEN];    // 艺术家(无标签为空串)
    char album[AUDIO_META_TAG_LEN];     // 专辑(无标签为空串)
} audio_meta_t;

/**
 * @brief 时间标签
 */
typedef struct {
    uint16_t base_s;    // 开始时间(s)
    uint8_t base_100ms; // 开始时间 (100ms)
} audio_time_t;

/**
 * @brief 元数据块类型
 */
typedef enum {
    AUDIO_META_VORBIS_COMMENT, // FLAC/OGG vorbis comment 原始块
    AUDIO_META_ID3V2,          // MP3 ID3v2 完整块
    AUDIO_META_ID3V1,          // MP3 ID3v1 块
    AUDIO_META_APE,            // MP3 APE 标签
    AUDIO_META_PICTURE,        // 封面图(FLAC PICTURE / ID3v2 APIC)
    AUDIO_META_STREAMINFO,     // FLAC STREAMINFO
    AUDIO_META_SEEKTABLE,      // FLAC SEEKTABLE
} audio_meta_type_t;

/**
 * @brief 元数据块(数据指针由 decoder 持有, 仅在回调期间有效, port 需要保留必须拷贝)
 */
typedef struct {
    audio_meta_type_t type; // 类型
    const void *data;       // 数据指针
    size_t size;            // 数据大小
} audio_meta_block_t;

/**
 * @brief port 注入的元数据回调, 每个 block 触发一次
 * @param user 回调用户数据
 * @param block 元数据块
 */
typedef void (*audio_meta_proc_t)(void *user, const audio_meta_block_t *block);

/**
 * @brief 音频解码器接口
 */
typedef struct {
	/**
     * @brief  打开音频文件, 透传所有元数据块
     * @param  path 文件路径
     * @param  onMeta 元数据回调(NULL 则不投递, 走无 metadata 快速路径)
     * @param  meta_user 回调用户数据
     * @param  dec 解码器句柄输出
     * @return 统一操作结果
     */
	audio_res_t (*open)(const uint8_t* path, audio_meta_proc_t onMeta,
		void *meta_user, void **dec);

     /**
     * @brief  关闭解码器并释放资源
     * @param  dec 解码器句柄
     * @return 统一操作结果
     */
	audio_res_t (*close)(void* dec);

     /**
     * @brief  读取PCM帧
     * @param  dec 解码器句柄
     * @param  buf  PCM数据缓冲区
     * @param  frames 读取帧数
     * @param  frames_read 实际读取帧数输出
     * @return 统一操作结果
     */
	audio_res_t (*read)(void* dec, int16_t* buf, uint32_t frames,
		uint32_t *frames_read);

	/**
     * @brief  跳转到指定PCM帧
     * @param  dec 解码器句柄
     * @param  frame 目标帧索引
     * @return 统一操作结果
     */
	audio_res_t (*seek)(void* dec, uint32_t frame);


	/**
     * @brief 获取音频元数据
     * @param dec 解码器句柄
     * @param info 音频元数据结构体指针
     * @return 统一操作结果
     */
	audio_res_t (*get_info)(void* dec, audio_meta_t* info);

} audio_decoder_t;








#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __AUDIO_PORT_H__ */
