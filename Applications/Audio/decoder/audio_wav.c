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
	FRESULT io_res; // 最近一次文件I/O结果
	bool alloc_failed; // 解码库内部分配失败
} wav_ctx_t;

/**
 * @brief dr_libs内存分配回调
 * @param sz 分配大小
 * @param pUserData 用户数据（未使用）
 * @return 分配的内存指针
 */
static void* wav_malloc(size_t sz, void* pUserData) {
	wav_ctx_t *ctx = (wav_ctx_t*)pUserData;
	void *p = pvPortMalloc(sz);
	if (p == NULL && ctx != NULL) ctx->alloc_failed = true;
	return p;
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
	wav_ctx_t *ctx = (wav_ctx_t*)pUserData;
	UINT br = 0;

	ctx->io_res = f_read(ctx->file, pBufferOut, (UINT)bytesToRead, &br);
	if (ctx->io_res != FR_OK) {
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
	wav_ctx_t *ctx = (wav_ctx_t*)pUserData;
	FIL* pFile = ctx->file;
	FSIZE_t pos = 0;

	switch (origin) {
		case DRWAV_SEEK_SET: pos = (FSIZE_t)offset; break;
		case DRWAV_SEEK_CUR: pos = (FSIZE_t)f_tell(pFile) + (FSIZE_t)offset; break;
		case DRWAV_SEEK_END: pos = (FSIZE_t)f_size(pFile) + (FSIZE_t)offset; break;
		default: return DRWAV_FALSE;
	}

	ctx->io_res = f_lseek(pFile, pos);
	return (ctx->io_res == FR_OK) ? DRWAV_TRUE : DRWAV_FALSE;
}

/**
 * @brief FATFS文件当前位置回调
 * @param pUserData FIL*文件句柄
 * @param pCursor 当前位置输出
 * @return 成功返回DRWAV_TRUE
 */
static drwav_bool32 wav_on_tell(void* pUserData, drwav_int64* pCursor) {
	wav_ctx_t *ctx = (wav_ctx_t*)pUserData;

	*pCursor = (drwav_int64)f_tell(ctx->file);
	return DRWAV_TRUE;
}

/**
 * @brief 打开wav文件
 * @param path 文件路径
 * @param onMeta 元数据回调(暂不支持)
 * @param meta_user 回调用户数据
 * @return 解码器句柄，失败返回NULL
 */
static audio_res_t wav_open(const uint8_t* path, audio_meta_proc_t onMeta,
	void *meta_user, void **dec) {
	drwav_allocation_callbacks alloc = wav_alloc;
	(void)onMeta; (void)meta_user;

	if (path == NULL || dec == NULL) return AUDIO_RES_INVALID_ARG;
	*dec = NULL;

	wav_ctx_t *ctx = pvPortMalloc(sizeof(wav_ctx_t));
	if (ctx == NULL) return AUDIO_RES_NO_MEMORY;
	memset(ctx, 0, sizeof(wav_ctx_t));
	alloc.pUserData = ctx;

	ctx->io_res = F_open(&ctx->file, path, FA_READ);
	if (ctx->io_res != FR_OK) {
		vPortFree(ctx);
		return AUDIO_RES_FS_OPEN_FAILED;
	}

	if (!drwav_init(&ctx->wav, fs_read, fs_seek, wav_on_tell, ctx, &alloc)) {
		audio_res_t res = ctx->alloc_failed ? AUDIO_RES_NO_MEMORY :
			((ctx->io_res == FR_OK) ? AUDIO_RES_DECODER_OPEN_FAILED :
			AUDIO_RES_FS_READ_FAILED);
		F_close(&ctx->file);
		vPortFree(ctx);
		return res;
	}

	*dec = ctx;
	return AUDIO_RES_OK;
}

/**
 * @brief 读取PCM帧
 * @param dec 解码器句柄
 * @param buf 输出缓冲
 * @param frames 请求读取的帧数
 * @return 实际读取的帧数
 */
static audio_res_t wav_read(void* dec, int16_t* buf, uint32_t frames,
	uint32_t *frames_read) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;
	if (ctx == NULL || buf == NULL || frames_read == NULL || frames == 0) {
		return AUDIO_RES_INVALID_ARG;
	}
	ctx->io_res = FR_OK;
	*frames_read = (uint32_t)drwav_read_pcm_frames_s16(&ctx->wav, frames, buf);
	if (ctx->io_res != FR_OK) return AUDIO_RES_FS_READ_FAILED;
	return (*frames_read == 0) ? AUDIO_RES_EOF : AUDIO_RES_OK;
}

/**
 * @brief 跳转到指定PCM帧
 * @param dec 解码器句柄
 * @param frame 目标帧索引
 * @return 统一操作结果
 */
static audio_res_t wav_seek(void* dec, uint32_t frame) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;
	if (ctx == NULL) return AUDIO_RES_INVALID_ARG;
	ctx->io_res = FR_OK;
	if (drwav_seek_to_pcm_frame(&ctx->wav, frame) == DRWAV_SUCCESS) return AUDIO_RES_OK;
	return (ctx->io_res == FR_OK) ? AUDIO_RES_DECODER_SEEK_FAILED :
		AUDIO_RES_FS_SEEK_FAILED;
}

/**
 * @brief 关闭解码器并释放资源
 * @param dec 解码器句柄
 */
static audio_res_t wav_close(void* dec) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;
	FRESULT res;
	if (ctx == NULL) return AUDIO_RES_INVALID_ARG;

	drwav_uninit(&ctx->wav);
	res = F_close(&ctx->file);
	vPortFree(ctx);
	return (res == FR_OK) ? AUDIO_RES_OK : AUDIO_RES_FS_CLOSE_FAILED;
}

/**
 * @brief 获取音频元数据
 * @param dec 解码器句柄
 * @param info 音频元数据输出
 */
static audio_res_t wav_get_meta(void* dec, audio_meta_t* info) {
	wav_ctx_t* ctx = (wav_ctx_t*)dec;
	if (ctx == NULL || info == NULL) return AUDIO_RES_INVALID_ARG;

	info->sample_rate = ctx->wav.sampleRate;
	info->channels = ctx->wav.channels;
	info->bits_per_sample = ctx->wav.bitsPerSample;
	info->total_frames = (uint64_t)ctx->wav.totalPCMFrameCount;
	info->duration_ms = (info->sample_rate > 0) ?
		(uint32_t)(info->total_frames * 1000 / info->sample_rate) : 0;
	return AUDIO_RES_OK;
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
