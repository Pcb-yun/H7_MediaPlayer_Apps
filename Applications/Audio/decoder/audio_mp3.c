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
	FRESULT io_res; // 最近一次文件I/O结果
	bool alloc_failed; // 解码库内部分配失败
} mp3_ctx_t;

/**
 * @brief dr_libs内存分配回调
 * @param sz 分配大小
 * @param pUserData 用户数据（未使用）
 * @return 分配的内存指针
 */
static void* mp3_malloc(size_t sz, void* pUserData) {
	mp3_ctx_t *ctx = (mp3_ctx_t*)pUserData;
	void *p = pvPortMalloc(sz);
	if (p == NULL && ctx != NULL) ctx->alloc_failed = true;
	return p;
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
 * @param pUserData mp3_ctx_t* 上下文
 * @param pBufferOut 输出缓冲
 * @param bytesToRead 请求读取的字节数
 * @return 实际读取的字节数
 */
static size_t fs_read(void* pUserData, void* pBufferOut, size_t bytesToRead) {
	mp3_ctx_t *ctx = (mp3_ctx_t*)pUserData;
	UINT br = 0;

	ctx->io_res = f_read(ctx->file, pBufferOut, (UINT)bytesToRead, &br);
	if (ctx->io_res != FR_OK) {
		return 0;
	}
	return (size_t)br;
}

/**
 * @brief FATFS文件定位回调
 * @param pUserData mp3_ctx_t* 上下文
 * @param offset 相对origin的偏移量
 * @param origin 定位基准
 * @retval enum drmp3_bool32
 */
static drmp3_bool32 fs_seek(void* pUserData, int offset, drmp3_seek_origin origin) {
	mp3_ctx_t *ctx = (mp3_ctx_t*)pUserData;
	FIL *pFile = ctx->file;
	FSIZE_t pos = 0;

	switch (origin) {
		case DRMP3_SEEK_SET: pos = (FSIZE_t)offset; break;
		case DRMP3_SEEK_CUR: pos = (FSIZE_t)f_tell(pFile) + (FSIZE_t)offset; break;
		case DRMP3_SEEK_END: pos = (FSIZE_t)f_size(pFile) + (FSIZE_t)offset; break;
		default: return DRMP3_FALSE;
	}

	ctx->io_res = f_lseek(pFile, pos);
	return (ctx->io_res == FR_OK) ? DRMP3_TRUE : DRMP3_FALSE;
}

/**
 * @brief FATFS文件当前位置回调
 * @param pUserData mp3_ctx_t* 上下文
 * @param pCursor 当前位置输出
 * @return 成功返回DRMP3_TRUE
 */
static drmp3_bool32 mp3_on_tell(void* pUserData, drmp3_int64* pCursor) {
	mp3_ctx_t *ctx = (mp3_ctx_t*)pUserData;

	*pCursor = (drmp3_int64)f_tell(ctx->file);
	return DRMP3_TRUE;
}

/**
 * @brief 打开mp3文件(仅播放, 不解析元数据)
 * @param path 文件路径
 * @param onMeta 元数据回调(未使用, 保留以匹配解码器接口)
 * @param meta_user 回调用户数据(未使用)
 * @return 解码器句柄, 失败返回NULL
 */
static audio_res_t mp3_open(const uint8_t* path, audio_meta_proc_t onMeta,
	void *meta_user, void **dec) {
	drmp3_allocation_callbacks alloc = mp3_alloc;
	(void)onMeta; (void)meta_user;   // MP3仅播放, 暂不解析元数据

	if (path == NULL || dec == NULL) return AUDIO_RES_INVALID_ARG;
	*dec = NULL;

	mp3_ctx_t *ctx = pvPortMalloc(sizeof(mp3_ctx_t));
	if (ctx == NULL) return AUDIO_RES_NO_MEMORY;
	memset(ctx, 0, sizeof(mp3_ctx_t));
	alloc.pUserData = ctx;

	ctx->io_res = F_open(&ctx->file, path, FA_READ);
	if (ctx->io_res != FR_OK) {
		vPortFree(ctx);
		return AUDIO_RES_FS_OPEN_FAILED;
	}

	if (!drmp3_init(&ctx->mp3, fs_read, fs_seek, mp3_on_tell, NULL, ctx, &alloc)) {
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
static audio_res_t mp3_read(void* dec, int16_t* buf, uint32_t frames,
	uint32_t *frames_read) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;
	if (ctx == NULL || buf == NULL || frames_read == NULL || frames == 0) {
		return AUDIO_RES_INVALID_ARG;
	}
	ctx->io_res = FR_OK;
	*frames_read = (uint32_t)drmp3_read_pcm_frames_s16(&ctx->mp3, frames, buf);
	if (ctx->io_res != FR_OK) return AUDIO_RES_FS_READ_FAILED;
	return (*frames_read == 0) ? AUDIO_RES_EOF : AUDIO_RES_OK;
}

/**
 * @brief 跳转到指定PCM帧
 * @param dec 解码器句柄
 * @param frame 目标帧索引
 * @return 统一操作结果
 */
static audio_res_t mp3_seek(void* dec, uint32_t frame) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;
	if (ctx == NULL) return AUDIO_RES_INVALID_ARG;
	ctx->io_res = FR_OK;
	if (drmp3_seek_to_pcm_frame(&ctx->mp3, frame) == DRMP3_TRUE) return AUDIO_RES_OK;
	return (ctx->io_res == FR_OK) ? AUDIO_RES_DECODER_SEEK_FAILED :
		AUDIO_RES_FS_SEEK_FAILED;
}

/**
 * @brief 关闭解码器并释放资源
 * @param dec 解码器句柄
 */
static audio_res_t mp3_close(void* dec) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;
	FRESULT res;
	if (ctx == NULL) return AUDIO_RES_INVALID_ARG;

	drmp3_uninit(&ctx->mp3);
	res = F_close(&ctx->file);
	vPortFree(ctx);
	return (res == FR_OK) ? AUDIO_RES_OK : AUDIO_RES_FS_CLOSE_FAILED;
}

/**
 * @brief 获取音频元数据
 * @param dec 解码器句柄
 * @param info 音频元数据输出
 */
static audio_res_t mp3_get_meta(void* dec, audio_meta_t* info) {
	mp3_ctx_t* ctx = (mp3_ctx_t*)dec;
	if (ctx == NULL || info == NULL) return AUDIO_RES_INVALID_ARG;

	info->sample_rate = ctx->mp3.sampleRate;
	info->channels = ctx->mp3.channels;
	info->bits_per_sample = 0;   // MP3为有损格式, 位深记为0
	info->total_frames = (ctx->mp3.totalPCMFrameCount == DRMP3_UINT64_MAX) ?
		0 : (uint64_t)ctx->mp3.totalPCMFrameCount;
	info->duration_ms = (info->sample_rate > 0) ?
		(uint32_t)(info->total_frames * 1000 / info->sample_rate) : 0;
	return AUDIO_RES_OK;
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
