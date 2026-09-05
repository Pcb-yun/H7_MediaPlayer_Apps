/**
 * @file audio_mp3.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief mp3格式音频解码源文件
 */

#include "audio_mp3.h"
#if AUDIO_SUPPORT_MP3

#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include "fatfs.h"
#include "FreeRTOS.h"
#include <string.h>

/* mp3解码器上下文 */
typedef struct {
	FIL* file;  // 文件句柄
	drmp3 mp3;  // dr_mp3解码器实例
} mp3_ctx_t;

/**
 * @brief dr_libs内存分配回调
 * @param sz 分配大小
 * @param pUserData 用户数据（未使用）
 * @return 分配的内存指针
 */
static void* mp3_malloc(size_t sz, void* pUserData) {
	(void)pUserData;
	return pvPortMalloc(sz);
}

/**
 * @brief dr_libs内存释放回调
 * @param p 待释放内存指针
 * @param pUserData 用户数据（未使用）
 */
static void mp3_free(void* p, void* pUserData) {
	(void)pUserData;
	vPortFree(p);
}

/* dr_libs内存分配回调集合 */
static const drmp3_allocation_callbacks mp3_alloc = {
	NULL, mp3_malloc, NULL, mp3_free
};

/**
 * @brief FATFS文件读回调
 * @param pUserData FIL*文件句柄
 * @param pBufferOut 输出缓冲
 * @param bytesToRead 请求读取的字节数
 * @return 实际读取的字节数
 */
static size_t fs_read(void* pUserData, void* pBufferOut, size_t bytesToRead) {
	FIL* pFile = (FIL*)pUserData;
	UINT br = 0;

	if (f_read(pFile, pBufferOut, (UINT)bytesToRead, &br) != FR_OK) {
		return 0;
	}
	return (size_t)br;
}

/**
 * @brief FATFS文件定位回调
 * @param pUserData FIL*文件句柄
 * @param offset 相对origin的偏移量
 * @param origin 定位基准
 * @retval enum drmp3_bool32
 */
static drmp3_bool32 fs_seek(void* pUserData, int offset, drmp3_seek_origin origin) {
	FIL* pFile = (FIL*)pUserData;
	FSIZE_t pos = 0;

	switch (origin) {
		case DRMP3_SEEK_SET: pos = (FSIZE_t)offset; break;
		case DRMP3_SEEK_CUR: pos = (FSIZE_t)f_tell(pFile) + (FSIZE_t)offset; break;
		case DRMP3_SEEK_END: pos = (FSIZE_t)f_size(pFile) + (FSIZE_t)offset; break;
		default: return DRMP3_FALSE;
	}

	return (f_lseek(pFile, pos) == FR_OK) ? DRMP3_TRUE : DRMP3_FALSE;
}

/**
 * @brief FATFS文件当前位置回调
 * @param pUserData FIL*文件句柄
 * @param pCursor 当前位置输出
 * @return 成功返回DRMP3_TRUE
 */
static drmp3_bool32 mp3_on_tell(void* pUserData, drmp3_int64* pCursor) {
	FIL* pFile = (FIL*)pUserData;

	*pCursor = (drmp3_int64)f_tell(pFile);
	return DRMP3_TRUE;
}

/**
 * @brief 打开mp3文件
 * @param path 文件路径
 * @return 解码器句柄，失败返回NULL
 */
static void* mp3_open(const uint8_t* path) {
	if (path == NULL) return NULL;

	mp3_ctx_t *ctx = pvPortMalloc(sizeof(mp3_ctx_t));
	if (ctx == NULL) return NULL;
	memset(ctx, 0, sizeof(mp3_ctx_t));

	if (F_open(&ctx->file, path, FA_READ) != FR_OK) {
		vPortFree(ctx);
		return NULL;
	}

	if (!drmp3_init(&ctx->mp3, fs_read, fs_seek, mp3_on_tell, NULL, ctx->file, &mp3_alloc)) {
		F_close(&ctx->file);
		vPortFree(ctx);
		return NULL;
	}

	return ctx;
}

/**
 * @brief 读取PCM帧
 * @param dec 解码器句柄
 * @param buf 输出缓冲
 * @param frames 请求读取的帧数
 * @return 实际读取的帧数
 */
static uint32_t mp3_read(void* dec, float* buf, uint32_t frames) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;
	return (uint32_t)drmp3_read_pcm_frames_f32(&ctx->mp3, frames, buf);
}

/**
 * @brief 跳转到指定PCM帧
 * @param dec 解码器句柄
 * @param frame 目标帧索引
 * @return 成功返回true，失败返回false
 */
static bool mp3_seek(void* dec, uint32_t frame) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;
	return drmp3_seek_to_pcm_frame(&ctx->mp3, frame) == DRMP3_TRUE;
}

/**
 * @brief 关闭解码器并释放资源
 * @param dec 解码器句柄
 */
static void mp3_close(void* dec) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;

	drmp3_uninit(&ctx->mp3);
	F_close(&ctx->file);
	vPortFree(ctx);
}

/**
 * @brief 获取音频元数据
 * @param dec 解码器句柄
 * @param info 音频元数据输出
 */
static void mp3_get_meta(void* dec, audio_meta_t* info) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;

	info->sample_rate = ctx->mp3.sampleRate;
	info->channels = ctx->mp3.channels;
	info->bits_per_sample = 0;   // MP3为有损格式, 位深记为0
	info->total_frames = (ctx->mp3.totalPCMFrameCount == DRMP3_UINT64_MAX) ?
		0 : (uint64_t)ctx->mp3.totalPCMFrameCount;
	info->duration_ms = (info->sample_rate > 0) ?
		(uint32_t)(info->total_frames * 1000 / info->sample_rate) : 0;
}

/* mp3解码器接口 */
const audio_decoder_t audio_mp3_decoder = {
	mp3_open,
	mp3_close,
	mp3_read,
	mp3_seek,
	mp3_get_meta,
};

#endif /* AUDIO_SUPPORT_MP3 */
