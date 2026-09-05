/**
 * @file audio_flac.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief flac格式音频解码源文件
 */

#include "audio_flac.h"
#if AUDIO_SUPPORT_FLAC

#define DR_FLAC_NO_STDIO
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include "fatfs.h"
#include "FreeRTOS.h"
#include <string.h>

/* flac解码器上下文 */
typedef struct {
	FIL* file;    // 文件句柄
	drflac* flac; // dr_flac解码器句柄
} flac_ctx_t;

/**
 * @brief dr_libs内存分配回调
 * @param sz 分配大小
 * @param pUserData 用户数据（未使用）
 * @return 分配的内存指针
 */
static void* flac_malloc(size_t sz, void* pUserData) {
	(void)pUserData;
	return pvPortMalloc(sz);
}

/**
 * @brief dr_libs内存释放回调
 * @param p 待释放内存指针
 * @param pUserData 用户数据（未使用）
 */
static void flac_free(void* p, void* pUserData) {
	(void)pUserData;
	vPortFree(p);
}

/* dr_libs内存分配回调集合 */
static const drflac_allocation_callbacks flac_alloc = {
	NULL, flac_malloc, NULL, flac_free
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
 * @retval enum drflac_bool32
 */
static drflac_bool32 fs_seek(void* pUserData, int offset, drflac_seek_origin origin) {
	FIL* pFile = (FIL*)pUserData;
	FSIZE_t pos = 0;

	switch (origin) {
		case DRFLAC_SEEK_SET: pos = (FSIZE_t)offset; break;
		case DRFLAC_SEEK_CUR: pos = (FSIZE_t)f_tell(pFile) + (FSIZE_t)offset; break;
		case DRFLAC_SEEK_END: pos = (FSIZE_t)f_size(pFile) + (FSIZE_t)offset; break;
		default: return DRFLAC_FALSE;
	}

	return (f_lseek(pFile, pos) == FR_OK) ? DRFLAC_TRUE : DRFLAC_FALSE;
}

/**
 * @brief FATFS文件当前位置回调
 * @param pUserData FIL*文件句柄
 * @param pCursor 当前位置输出
 * @return 成功返回DRFLAC_TRUE
 */
static drflac_bool32 flac_on_tell(void* pUserData, drflac_int64* pCursor) {
	FIL* pFile = (FIL*)pUserData;

	*pCursor = (drflac_int64)f_tell(pFile);
	return DRFLAC_TRUE;
}

/**
 * @brief 打开flac文件
 * @param path 文件路径
 * @return 解码器句柄，失败返回NULL
 */
static void* flac_open(const uint8_t* path) {
	if (path == NULL) return NULL;

	flac_ctx_t *ctx = pvPortMalloc(sizeof(flac_ctx_t));
	if (ctx == NULL) return NULL;
	memset(ctx, 0, sizeof(flac_ctx_t));

	if (F_open(&ctx->file, path, FA_READ) != FR_OK) {
		vPortFree(ctx);
		return NULL;
	}

	ctx->flac = drflac_open(fs_read, fs_seek, flac_on_tell, ctx->file, &flac_alloc);
	if (ctx->flac == NULL) {
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
static uint32_t flac_read(void* dec, float* buf, uint32_t frames) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;
#if !AUDIO_SKEEP_FAILFRAME
	return (uint32_t)drflac_read_pcm_frames_f32(ctx->flac, frames, buf);
#else
	uint32_t n = (uint32_t)drflac_read_pcm_frames_f32(ctx->flac, frames, buf);

	if (n == 0 && ctx->flac->currentPCMFrame < ctx->flac->totalPCMFrameCount) {
		uint64_t next = ctx->flac->currentPCMFrame + ctx->flac->maxBlockSizeInPCMFrames;
		if (drflac_seek_to_pcm_frame(ctx->flac, next)) {
			n = (uint32_t)drflac_read_pcm_frames_f32(ctx->flac, frames, buf);
		}
	}
	return n;
#endif
}

/**
 * @brief 跳转到指定PCM帧
 * @param dec 解码器句柄
 * @param frame 目标帧索引
 * @return 成功返回true，失败返回false
 */
static bool flac_seek(void* dec, uint32_t frame) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;
	return drflac_seek_to_pcm_frame(ctx->flac, frame) == DRFLAC_TRUE;
}

/**
 * @brief 关闭解码器并释放资源
 * @param dec 解码器句柄
 */
static void flac_close(void* dec) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;

	drflac_close(ctx->flac);
	F_close(&ctx->file);
	vPortFree(ctx);
}

/**
 * @brief 获取音频元数据
 * @param dec 解码器句柄
 * @param info 音频元数据输出
 */
static void flac_get_meta(void* dec, audio_meta_t* info) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;

	info->sample_rate = ctx->flac->sampleRate;
	info->channels = ctx->flac->channels;
	info->bits_per_sample = ctx->flac->bitsPerSample;
	info->total_frames = (uint64_t)ctx->flac->totalPCMFrameCount;
	info->duration_ms = (info->sample_rate > 0) ?
		(uint32_t)(info->total_frames * 1000 / info->sample_rate) : 0;
}

/* flac解码器接口 */
const audio_decoder_t audio_flac_decoder = {
	flac_open,
	flac_close,
	flac_read,
	flac_seek,
	flac_get_meta,
};

#endif /* AUDIO_SUPPORT_FLAC */
