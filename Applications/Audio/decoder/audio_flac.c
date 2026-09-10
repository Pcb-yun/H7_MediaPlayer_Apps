/**
 * @file audio_flac.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief flac格式音频解码源文件
 */

#include "audio_flac.h"
#if AUDIO_SUPPORT_FLAC

#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_PICTURE_METADATA_MALLOC
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include "fatfs.h"
#include "FreeRTOS.h"
#include <string.h>

/* flac解码器上下文 */
typedef struct {
	FIL* file;  // 文件句柄
	drflac* flac; // dr_flac解码器句柄
	audio_meta_proc_t meta_cb; // 元数据回调(NULL 走无 metadata 快速路径)
	void *meta_user; // 元数据回调用户数据
	FRESULT io_res; // 最近一次文件I/O结果
	bool alloc_failed; // 解码库内部分配失败
} flac_ctx_t;

/**
 * @brief dr_libs内存分配回调
 * @param sz 分配大小
 * @param pUserData 用户数据（未使用）
 * @return 分配的内存指针
 */
static void* flac_malloc(size_t sz, void* pUserData) {
	flac_ctx_t *ctx = (flac_ctx_t*)pUserData;
	void *p = pvPortMalloc(sz);
	if (p == NULL && ctx != NULL) ctx->alloc_failed = true;
	return p;
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
 * @param pUserData flac_ctx_t* 上下文(dr_flac 的 pUserData 同时喂给 onRead/onMeta)
 * @param pBufferOut 输出缓冲
 * @param bytesToRead 请求读取的字节数
 * @return 实际读取的字节数
 */
static size_t fs_read(void* pUserData, void* pBufferOut, size_t bytesToRead) {
	flac_ctx_t *ctx = (flac_ctx_t*)pUserData;
	UINT br = 0;

	ctx->io_res = f_read(ctx->file, pBufferOut, (UINT)bytesToRead, &br);
	if (ctx->io_res != FR_OK) {
		return 0;
	}
	return (size_t)br;
}

/**
 * @brief FATFS文件定位回调
 * @param pUserData flac_ctx_t* 上下文
 * @param offset 相对origin的偏移量
 * @param origin 定位基准
 * @retval enum drflac_bool32
 */
static drflac_bool32 fs_seek(void* pUserData, int offset, drflac_seek_origin origin) {
	flac_ctx_t *ctx = (flac_ctx_t*)pUserData;
	FIL *pFile = ctx->file;
	FSIZE_t pos = 0;

	switch (origin) {
		case DRFLAC_SEEK_SET: pos = (FSIZE_t)offset; break;
		case DRFLAC_SEEK_CUR: pos = (FSIZE_t)f_tell(pFile) + (FSIZE_t)offset; break;
		case DRFLAC_SEEK_END: pos = (FSIZE_t)f_size(pFile) + (FSIZE_t)offset; break;
		default: return DRFLAC_FALSE;
	}

	ctx->io_res = f_lseek(pFile, pos);
	return (ctx->io_res == FR_OK) ? DRFLAC_TRUE : DRFLAC_FALSE;
}

/**
 * @brief FATFS文件当前位置回调
 * @param pUserData flac_ctx_t* 上下文
 * @param pCursor 当前位置输出
 * @return 成功返回DRFLAC_TRUE
 */
static drflac_bool32 flac_on_tell(void* pUserData, drflac_int64* pCursor) {
	flac_ctx_t *ctx = (flac_ctx_t*)pUserData;

	*pCursor = (drflac_int64)f_tell(ctx->file);
	return DRFLAC_TRUE;
}

/**
 * @brief drflac metadata 透传 adapter: 把 drflac_metadata 转为 audio_meta_block_t 投递给 port
 * @param pUserData flac_ctx_t* 上下文(回调与用户数据直接存于 ctx)
 * @param pMetadata drflac 元数据
 */
static void flac_on_meta(void *pUserData, drflac_metadata *pMetadata) {
	flac_ctx_t *ctx = (flac_ctx_t*)pUserData;
	audio_meta_block_t blk;

	if (ctx == NULL || ctx->meta_cb == NULL || pMetadata == NULL) return;

	switch (pMetadata->type) {
		case DRFLAC_METADATA_BLOCK_TYPE_STREAMINFO:
			blk.type = AUDIO_META_STREAMINFO;
			blk.data = pMetadata->pRawData;
			blk.size = pMetadata->rawDataSize;
			break;
		case DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT:
			blk.type = AUDIO_META_VORBIS_COMMENT;
			blk.data = pMetadata->pRawData;
			blk.size = pMetadata->rawDataSize;
			break;
		case DRFLAC_METADATA_BLOCK_TYPE_SEEKTABLE:
			blk.type = AUDIO_META_SEEKTABLE;
			blk.data = pMetadata->pRawData;
			blk.size = pMetadata->rawDataSize;
			break;
		case DRFLAC_METADATA_BLOCK_TYPE_PICTURE:
			blk.type = AUDIO_META_PICTURE;
			blk.data = pMetadata->data.picture.pPictureData;
			blk.size = pMetadata->data.picture.pictureDataSize;
			break;
		default:
			return; // PADDING/APPLICATION/CUESHEET 暂不投递
	}
	if (blk.data == NULL && pMetadata->pRawData != NULL) {
		blk.data = pMetadata->pRawData;
		blk.size = pMetadata->rawDataSize;
	}
	ctx->meta_cb(ctx->meta_user, &blk);
}

/**
 * @brief 打开flac文件, 透传所有元数据块
 * @param path 文件路径
 * @param onMeta 元数据回调(NULL 走无 metadata 快速路径)
 * @param meta_user 回调用户数据
 * @return 解码器句柄, 失败返回NULL
 */
static audio_res_t flac_open(const uint8_t* path, audio_meta_proc_t onMeta,
	void *meta_user, void **dec) {
	drflac_allocation_callbacks alloc = flac_alloc;
	if (path == NULL || dec == NULL) return AUDIO_RES_INVALID_ARG;
	*dec = NULL;

	flac_ctx_t *ctx = pvPortMalloc(sizeof(flac_ctx_t));
	if (ctx == NULL) return AUDIO_RES_NO_MEMORY;
	memset(ctx, 0, sizeof(flac_ctx_t));
	ctx->meta_cb = onMeta;
	ctx->meta_user = meta_user;
	alloc.pUserData = ctx;

	ctx->io_res = F_open(&ctx->file, path, FA_READ);
	if (ctx->io_res != FR_OK) {
		vPortFree(ctx);
		return AUDIO_RES_FS_OPEN_FAILED;
	}

	if (onMeta != NULL) {
		ctx->flac = drflac_open_with_metadata(fs_read, fs_seek, flac_on_tell,
			flac_on_meta, ctx, &alloc);
	} else {
		ctx->flac = drflac_open(fs_read, fs_seek, flac_on_tell, ctx, &alloc);
	}

	if (ctx->flac == NULL) {
		/*
		 * 某些文件的音频帧有效，但 metadata 块损坏。
		 * 带 metadata 打开失败时回到文件头，退化为仅解码音频。
		 */
		if (onMeta != NULL && !ctx->alloc_failed && ctx->io_res == FR_OK) {
			ctx->io_res = f_lseek(ctx->file, 0);
			if (ctx->io_res == FR_OK) {
				ctx->flac = drflac_open(fs_read, fs_seek, flac_on_tell, ctx, &alloc);
			}
		}
	}

	if (ctx->flac == NULL) {
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
static audio_res_t flac_read(void* dec, int16_t* buf, uint32_t frames,
	uint32_t *frames_read) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;
	uint32_t n;
	if (ctx == NULL || buf == NULL || frames_read == NULL || frames == 0) {
		return AUDIO_RES_INVALID_ARG;
	}
	ctx->io_res = FR_OK;
#if !AUDIO_SKEEP_FAILFRAME
	n = (uint32_t)drflac_read_pcm_frames_s16(ctx->flac, frames, buf);
#else
	n = (uint32_t)drflac_read_pcm_frames_s16(ctx->flac, frames, buf);

	if (n == 0 && ctx->io_res == FR_OK &&
		ctx->flac->currentPCMFrame < ctx->flac->totalPCMFrameCount) {
		uint64_t next = ctx->flac->currentPCMFrame + ctx->flac->maxBlockSizeInPCMFrames;
		if (!drflac_seek_to_pcm_frame(ctx->flac, next)) {
			*frames_read = 0;
			return (ctx->io_res == FR_OK) ? AUDIO_RES_DECODE_FAILED :
				AUDIO_RES_FS_SEEK_FAILED;
		}
		n = (uint32_t)drflac_read_pcm_frames_s16(ctx->flac, frames, buf);
	}
#endif
	*frames_read = n;
	if (ctx->io_res != FR_OK) return AUDIO_RES_FS_READ_FAILED;
	if (n > 0) return AUDIO_RES_OK;
	return (ctx->flac->currentPCMFrame < ctx->flac->totalPCMFrameCount) ?
		AUDIO_RES_DECODE_FAILED : AUDIO_RES_EOF;
}

/**
 * @brief 跳转到指定PCM帧
 * @param dec 解码器句柄
 * @param frame 目标帧索引
 * @return 统一操作结果
 */
static audio_res_t flac_seek(void* dec, uint32_t frame) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;
	if (ctx == NULL) return AUDIO_RES_INVALID_ARG;
	ctx->io_res = FR_OK;
	if (drflac_seek_to_pcm_frame(ctx->flac, frame) == DRFLAC_TRUE) return AUDIO_RES_OK;
	return (ctx->io_res == FR_OK) ? AUDIO_RES_DECODER_SEEK_FAILED :
		AUDIO_RES_FS_SEEK_FAILED;
}

/**
 * @brief 关闭解码器并释放资源
 * @param dec 解码器句柄
 */
static audio_res_t flac_close(void* dec) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;
	FRESULT res;
	if (ctx == NULL) return AUDIO_RES_INVALID_ARG;

	drflac_close(ctx->flac);
	res = F_close(&ctx->file);
	vPortFree(ctx);
	return (res == FR_OK) ? AUDIO_RES_OK : AUDIO_RES_FS_CLOSE_FAILED;
}

/**
 * @brief 获取音频元数据
 * @param dec 解码器句柄
 * @param info 音频元数据输出
 */
static audio_res_t flac_get_meta(void* dec, audio_meta_t* info) {
	flac_ctx_t* ctx = (flac_ctx_t*)dec;
	if (ctx == NULL || info == NULL) return AUDIO_RES_INVALID_ARG;

	info->sample_rate = ctx->flac->sampleRate;
	info->channels = ctx->flac->channels;
	info->bits_per_sample = ctx->flac->bitsPerSample;
	info->total_frames = (uint64_t)ctx->flac->totalPCMFrameCount;
	info->duration_ms = (info->sample_rate > 0) ?
		(uint32_t)(info->total_frames * 1000 / info->sample_rate) : 0;
	return AUDIO_RES_OK;
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
