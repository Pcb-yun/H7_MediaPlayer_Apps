/**
 * @file audio_wav.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief wav格式音频解码源文件
 */

#include "audio_wav.h"
#if AUDIO_SUPPORT_WAV

#define DR_WAV_NO_STDIO
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "fatfs.h"
#include "FreeRTOS.h"
#include <string.h>

/* wav解码器上下文 */
typedef struct {
	FIL* file;  // 文件句柄
	drwav wav;  // dr_wav解码器实例
} wav_ctx_t;

/**
 * @brief dr_libs内存分配回调
 * @param sz 分配大小
 * @param pUserData 用户数据（未使用）
 * @return 分配的内存指针
 */
static void* wav_malloc(size_t sz, void* pUserData) {
	(void)pUserData;
	return pvPortMalloc(sz);
}

/**
 * @brief dr_libs内存释放回调
 * @param p 待释放内存指针
 * @param pUserData 用户数据（未使用）
 */
static void wav_free(void* p, void* pUserData) {
	(void)pUserData;
	vPortFree(p);
}

/* dr_libs内存分配回调集合 */
static const drwav_allocation_callbacks wav_alloc = {
	NULL, wav_malloc, NULL, wav_free
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
 * @retval enum drwav_bool32
 */
static drwav_bool32 fs_seek(void* pUserData, int offset, drwav_seek_origin origin) {
	FIL* pFile = (FIL*)pUserData;
	FSIZE_t pos = 0;

	switch (origin) {
		case DRWAV_SEEK_SET: pos = (FSIZE_t)offset; break;
		case DRWAV_SEEK_CUR: pos = (FSIZE_t)f_tell(pFile) + (FSIZE_t)offset; break;
		case DRWAV_SEEK_END: pos = (FSIZE_t)f_size(pFile) + (FSIZE_t)offset; break;
		default: return DRWAV_FALSE;
	}

	return (f_lseek(pFile, pos) == FR_OK) ? DRWAV_TRUE : DRWAV_FALSE;
}

/**
 * @brief FATFS文件当前位置回调
 * @param pUserData FIL*文件句柄
 * @param pCursor 当前位置输出
 * @return 成功返回DRWAV_TRUE
 */
static drwav_bool32 wav_on_tell(void* pUserData, drwav_int64* pCursor) {
	FIL* pFile = (FIL*)pUserData;

	*pCursor = (drwav_int64)f_tell(pFile);
	return DRWAV_TRUE;
}

/**
 * @brief 打开wav文件
 * @param path 文件路径
 * @return 解码器句柄，失败返回NULL
 */
static void* wav_open(const uint8_t* path) {
	if (path == NULL) return NULL;

	wav_ctx_t *ctx = pvPortMalloc(sizeof(wav_ctx_t));
	if (ctx == NULL) return NULL;
	memset(ctx, 0, sizeof(wav_ctx_t));

	if (F_open(&ctx->file, path, FA_READ) != FR_OK) {
		vPortFree(ctx);
		return NULL;
	}

	if (!drwav_init(&ctx->wav, fs_read, fs_seek, wav_on_tell, ctx->file, &wav_alloc)) {
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
static uint32_t wav_read(void* dec, float* buf, uint32_t frames) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;
	return (uint32_t)drwav_read_pcm_frames_f32(&ctx->wav, frames, buf);
}

/**
 * @brief 跳转到指定PCM帧
 * @param dec 解码器句柄
 * @param frame 目标帧索引
 * @return 成功返回0，失败返回-1
 */
static bool wav_seek(void* dec, uint32_t frame) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;
	return drwav_seek_to_pcm_frame(&ctx->wav, frame) == DRWAV_SUCCESS;
}

/**
 * @brief 关闭解码器并释放资源
 * @param dec 解码器句柄
 */
static void wav_close(void* dec) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;

	drwav_uninit(&ctx->wav);
	F_close(&ctx->file);
	vPortFree(ctx);
}

/**
 * @brief 获取音频元数据
 * @param dec 解码器句柄
 * @param info 音频元数据输出
 */
static void wav_get_meta(void* dec, audio_meta_t* info) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;

	info->sample_rate = ctx->wav.sampleRate;
	info->channels = ctx->wav.channels;
	info->bits_per_sample = ctx->wav.bitsPerSample;
	info->total_frames = (uint64_t)ctx->wav.totalPCMFrameCount;
	info->duration_ms = (info->sample_rate > 0) ?
		(uint32_t)(info->total_frames * 1000 / info->sample_rate) : 0;
}

/* wav解码器接口 */
const audio_decoder_t audio_wav_decoder = {
	wav_open,
	wav_close,
	wav_read,
	wav_seek,
	wav_get_meta,
};

#endif /* AUDIO_SUPPORT_WAV */
