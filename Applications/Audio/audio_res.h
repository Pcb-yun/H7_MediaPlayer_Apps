/**
 * @file audio_res.h
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief Audio模块统一返回值
 */

#ifndef __AUDIO_RES_H__
#define __AUDIO_RES_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio模块统一操作结果
 */
typedef enum {
	AUDIO_RES_OK = 0,
	AUDIO_RES_EOF,
	AUDIO_RES_INVALID_ARG,
	AUDIO_RES_UNSUPPORTED_FORMAT,
	AUDIO_RES_UNSUPPORTED_CHANNELS,
	AUDIO_RES_UNSUPPORTED_SAMPLE_RATE,
	AUDIO_RES_NO_MEMORY,
	AUDIO_RES_BUFFER_TOO_SMALL,
	AUDIO_RES_FS_OPEN_FAILED,
	AUDIO_RES_FS_READ_FAILED,
	AUDIO_RES_FS_SEEK_FAILED,
	AUDIO_RES_FS_CLOSE_FAILED,
	AUDIO_RES_DECODER_OPEN_FAILED,
	AUDIO_RES_DECODE_FAILED,
	AUDIO_RES_DECODER_SEEK_FAILED,
	AUDIO_RES_METADATA_FAILED,
	AUDIO_RES_SAI_CONFIG_FAILED,
	AUDIO_RES_DMA_SIZE_INVALID,
	AUDIO_RES_DMA_START_FAILED,
	AUDIO_RES_DMA_CONTROL_FAILED,
	AUDIO_RES_DMA_TIMEOUT,
	AUDIO_RES_SEMAPHORE_FAILED,
	AUDIO_RES_EMPTY_STREAM,
	AUDIO_RES_ABORTED,
} audio_res_t;

/**
 * @brief 获取统一结果的可读文本
 */
static const __attribute__((section(".W25Q64")))
char *audio_res_str(audio_res_t res) {
	switch (res) {
		case AUDIO_RES_OK: return "ok";
		case AUDIO_RES_EOF: return "end of stream";
		case AUDIO_RES_INVALID_ARG: return "invalid argument";
		case AUDIO_RES_UNSUPPORTED_FORMAT: return "unsupported format";
		case AUDIO_RES_UNSUPPORTED_CHANNELS: return "unsupported channels";
		case AUDIO_RES_UNSUPPORTED_SAMPLE_RATE: return "unsupported sample rate";
		case AUDIO_RES_NO_MEMORY: return "out of memory";
		case AUDIO_RES_BUFFER_TOO_SMALL: return "audio buffer below safe duration";
		case AUDIO_RES_FS_OPEN_FAILED: return "file open failed";
		case AUDIO_RES_FS_READ_FAILED: return "file read failed";
		case AUDIO_RES_FS_SEEK_FAILED: return "file seek failed";
		case AUDIO_RES_FS_CLOSE_FAILED: return "file close failed";
		case AUDIO_RES_DECODER_OPEN_FAILED: return "decoder open failed";
		case AUDIO_RES_DECODE_FAILED: return "decode failed";
		case AUDIO_RES_DECODER_SEEK_FAILED: return "decoder seek failed";
		case AUDIO_RES_METADATA_FAILED: return "metadata parse failed";
		case AUDIO_RES_SAI_CONFIG_FAILED: return "SAI configuration failed";
		case AUDIO_RES_DMA_SIZE_INVALID: return "SAI DMA buffer is too large";
		case AUDIO_RES_DMA_START_FAILED: return "SAI DMA start failed";
		case AUDIO_RES_DMA_CONTROL_FAILED: return "SAI DMA pause/resume failed";
		case AUDIO_RES_DMA_TIMEOUT: return "SAI DMA timeout";
		case AUDIO_RES_SEMAPHORE_FAILED: return "semaphore operation failed";
		case AUDIO_RES_EMPTY_STREAM: return "empty audio stream";
		case AUDIO_RES_ABORTED: return "aborted";
		default: return "unknown audio result";
	}
}


#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_RES_H__ */
