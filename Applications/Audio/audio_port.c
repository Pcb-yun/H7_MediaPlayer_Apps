/**
 * @file audio_port.c
 * @author Pcb-yun (pcbyinyun@163.com)
 * @brief 音频解码播放接口层源文件
 */

#include "audio_port.h"
#include "audio_tui.h"
#include "shell.h"
#include "shell_port.h"
#include "log.h"
#include "sai.h"
#include "shell_cmd_group.h"
#include "Events.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include <string.h>
#include <stdio.h>


#if AUDIO_SUPPORT_WAV
#include "audio_wav.h"
#endif /* AUDIO_SUPPORT_WAV */
#if AUDIO_SUPPORT_MP3
#include "audio_mp3.h"
#endif /* AUDIO_SUPPORT_MP3 */
#if AUDIO_SUPPORT_FLAC
#include "audio_flac.h"
#endif /* AUDIO_SUPPORT_FLAC */
#if AUDIO_SUPPORT_LRC
#include "audio_lrc.h"
#endif /* AUDIO_SUPPORT_LRC */

/**
 * @brief 播放器运行状态
 */
typedef struct {
	const audio_decoder_t* dec;		// 当前解码器接口
	audio_meta_t meta;				// 当前音频元数据
	void* dec_ctx;					// 当前解码器句柄
	int16_t* pcm;					// 解码16-bit PCM缓冲
	uint32_t* tx[AUDIO_PLAY_CH];	// SAI输出缓冲
	uint8_t channels;				// 当前声道数
	uint32_t frames;				// 每半区PCM帧数
	uint32_t tx_size;				// 单个tx缓冲元素数
	uint32_t valid_frames[2];		// 两个DMA半区内的有效PCM帧数
	osSemaphoreId_t sem;			// DMA半区空闲信号量
	volatile bool dma_running;		// 仅在DMA有效运行时接收完成通知
	audio_time_t cur_time;			// 当前播放时间
	bool meta_only;					// 仅读元数据模式(跳过歌词收集)
} audio_port_t;

static audio_port_t* g_port = NULL;			// 当前播放器实例
static uint8_t volume = AUDIO_DEFAULT_VOLUME; // 音量(0-100)
static audio_res_t audio_set_freq(uint32_t sample_rate);


/**
 * @brief 小端32位读取
 * @param p 数据指针
 * @return 32位值
 */
static uint32_t audio_le32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief 大小写不敏感前缀比较
 * @param s 待比较字符串
 * @param key 关键字
 * @param n 比较长度
 * @return 相等返回0
 */
static int16_t audio_prefix_icmp(const char *s, const char *key, size_t n) {
	size_t i;
	for (i = 0; i < n; i++) {
		char a = s[i], b = key[i];
		if (a >= 'A' && a <= 'Z') a += 32;
		if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return (int16_t)(a - b);
    }
	return 0;
}

/**
 * @brief 拷贝标签文本到目标
 * @param dst 目标缓冲
 * @param cap 目标容量
 * @param src 源数据
 * @param len 源长度
 */
static void audio_tag_copy(char *dst, uint32_t cap, const char *src, uint32_t len) {
	uint32_t n = (len < cap - 1) ? len : cap - 1;
	if (n > 0) memcpy(dst, src, n);
	dst[n] = '\0';
}

#if AUDIO_SUPPORT_FLAC
/**
 * @brief 从FLAC VORBIS_COMMENT块中提取歌名/艺术家/专辑
 * @param port 播放器实例
 * @param data VORBIS_COMMENT 原始块数据
 * @param size 数据大小
 */
static void audio_meta_vorbis(audio_port_t *port, const void *data, uint32_t size) {
	const uint8_t *p = (const uint8_t*)data;
	uint32_t remain = size, count, i;

#if AUDIO_SUPPORT_LRC
	bool lrc_done = false;
#endif

	if (port == NULL || data == NULL) return;

	// 跳过 vendor 字符串
	if (remain < 4) return;
	uint32_t vlen = audio_le32(p);
	p += 4; remain -= 4;
	if (remain < vlen) return;
	p += vlen; remain -= vlen;

	// comment_count
	if (remain < 4) return;
	count = audio_le32(p);
	p += 4; remain -= 4;

	for (i = 0; i < count; i++) {
		uint32_t clen;
		const char *cs;
		uint32_t val_len;
		const char *val;

		if (remain < 4) return;
		clen = audio_le32(p);
		p += 4; remain -= 4;
		if (remain < clen) return;
		cs = (const char*)p;

		// TITLE=
		if (port->meta.title[0] == '\0' && clen > 6 &&
			audio_prefix_icmp(cs, "TITLE=", 6) == 0) {
			val = cs + 6; val_len = clen - 6;
			audio_tag_copy(port->meta.title, sizeof(port->meta.title), val, val_len);
		}
		// ARTIST=
		if (port->meta.artist[0] == '\0' && clen > 7 &&
			audio_prefix_icmp(cs, "ARTIST=", 7) == 0) {
			val = cs + 7; val_len = clen - 7;
			audio_tag_copy(port->meta.artist, sizeof(port->meta.artist), val, val_len);
		}
		// ALBUM=
		if (port->meta.album[0] == '\0' && clen > 6 &&
			audio_prefix_icmp(cs, "ALBUM=", 6) == 0) {
			val = cs + 6; val_len = clen - 6;
			audio_tag_copy(port->meta.album, sizeof(port->meta.album), val, val_len);
		}

#if AUDIO_SUPPORT_LRC
		// 歌词字段: 按 LYRICS / UNSYNCEDLYRICS / SYNCEDLYRICS 顺序匹配首个
        if (!port->meta_only && !lrc_done) {
			uint32_t ll = 0;
			const char *lv = NULL;
			if (clen > 7 && audio_prefix_icmp(cs, "LYRICS=", 7) == 0) {
				lv = cs + 7; ll = clen - 7;
			} else if (clen > 15 && audio_prefix_icmp(cs, "UNSYNCEDLYRICS=", 15) == 0) {
				lv = cs + 15; ll = clen - 15;
			} else if (clen > 14 && audio_prefix_icmp(cs, "SYNCEDLYRICS=", 14) == 0) {
				lv = cs + 14; ll = clen - 14;
			}
			if (lv != NULL && ll > 0) {
				uint8_t *buf = (uint8_t*)pvPortMalloc(ll + 1);
				if (buf != NULL) {
					memcpy(buf, lv, ll);
					buf[ll] = '\0';
					Lrc_register(buf, ll);
					lrc_done = true;
				}
			}
		}
#endif

		p += clen; remain -= clen;
	}
}
#endif

/**
 * @brief 播放器元数据回调
 * @param user 回调用户数据(audio_port_t*)
 * @param block 元数据块
 */
static void port_on_meta(void *user, const audio_meta_block_t *block) {
	audio_port_t *port = (audio_port_t*)user;

	if (block == NULL || port == NULL) return;
	switch (block->type) {
#if AUDIO_SUPPORT_FLAC
		case AUDIO_META_VORBIS_COMMENT:
			audio_meta_vorbis(port, block->data, (uint32_t)block->size);
			break;
#endif
		default:
			break;
	}
}

/**
 * @brief 计算每半区PCM帧数(按目标时长分配, 内存不足时自动缩小)
 * @param channels 声道数
 * @param sample_rate 采样率
 * @return 帧数, 0表示可用内存不足
 */
static uint32_t audio_calc_frames(uint8_t channels, uint32_t sample_rate) {
	HeapStats_t stats;
	size_t largest;
	uint32_t bytes_per_frame;
	uint32_t capacity;
	uint32_t dma_capacity;
	uint32_t min_frames;
	uint32_t frames;

	vPortGetHeapStats(&stats);
	// heap_4按连续块分配, 用最大空闲块而非总空闲(总空闲可能被碎片分成多块)
	largest = stats.xSizeOfLargestFreeBlockInBytes;
	if (largest <= AUDIO_RESERVED_MEM) return 0;

	// PCM保留原声道数, SAI始终输出AUDIO_PLAY_CH声道的双缓冲。
	bytes_per_frame = channels * sizeof(int16_t) +
		2u * AUDIO_PLAY_CH * sizeof(uint32_t);
	capacity = (uint32_t)((largest - AUDIO_RESERVED_MEM) / bytes_per_frame);

	/*
	 * HAL_SAI_Transmit_DMA() 的 Size 为 uint16_t，且长度单位是32位slot。
	 * 整个循环DMA包含两个半区，每帧各有 AUDIO_PLAY_CH 个slot。
	 * 若超过65535，参数会在进入HAL前被截断，硬件回卷点将与软件半区错位。
	 */
	dma_capacity = 0xFFFFu / (2u * AUDIO_PLAY_CH);
	if (capacity > dma_capacity) capacity = dma_capacity;
	capacity &= ~3u;
	min_frames = (sample_rate * AUDIO_BUFFER_MIN_MS + 999u) / 1000u;
	min_frames = (min_frames + 3u) & ~3u;
	if (capacity < min_frames) return 0;

	frames = (sample_rate * AUDIO_BUFFER_TARGET_MS + 999u) / 1000u;
	if (frames > capacity) frames = capacity;
	frames &= ~3u;   // 向下取整到4的倍数, 保持声道对齐
	return frames;
}

/**
 * @brief 按指定帧数一次性分配pcm/tx缓冲
 * @param frames 每半区PCM帧数
 * @return 统一操作结果
 */
static audio_res_t audio_buf_alloc(uint32_t frames) {
	audio_port_t* port = g_port;
	uint8_t *base;
	uint32_t tx_bytes, pcm_bytes;

	port->frames = frames;
	port->tx_size = frames * AUDIO_PLAY_CH;

	tx_bytes = port->tx_size * 2 * sizeof(uint32_t);
    pcm_bytes = frames * port->channels * sizeof(int16_t);

	base = (uint8_t*)pvPortMalloc(tx_bytes + pcm_bytes);
	if (base == NULL) return AUDIO_RES_NO_MEMORY;

	port->tx[0] = (uint32_t*)base;
	port->tx[1] = port->tx[0] + port->tx_size;
	port->pcm = (int16_t*)(base + tx_bytes);
	return AUDIO_RES_OK;
}

/**
 * @brief 释放播放器实例及内部资源
 */
static audio_res_t audio_port_free(void) {
	audio_res_t result = AUDIO_RES_OK;
	audio_res_t res;
	if (g_port == NULL) return AUDIO_RES_OK;

    if (g_port->dec_ctx != NULL) {
		res = g_port->dec->close(g_port->dec_ctx);
		if (res != AUDIO_RES_OK) result = res;
    }

#if AUDIO_SUPPORT_LRC
    Lrc_free();
#endif

    if (g_port->sem != NULL) {
		if (osSemaphoreDelete(g_port->sem) != osOK && result == AUDIO_RES_OK) {
			result = AUDIO_RES_SEMAPHORE_FAILED;
		}
    }

	if (g_port->tx[0] != NULL) {
		vPortFree(g_port->tx[0]);
	}

    vPortFree(g_port);
    g_port = NULL;
	return result;
}

/**
 * @brief 关闭播放器并释放全部资源
 */
static audio_res_t audio_close(void) {
	audio_res_t res;
	if (g_port != NULL) g_port->dma_running = false;
	HAL_SAI_DMAStop(&hsai_BlockA1);
	res = audio_port_free();
	tui_clear();
	osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
	return res;
}

/**
 * @brief 关闭播放器并保留更早发生的主错误
 */
static audio_res_t audio_finish(audio_res_t result) {
	audio_res_t close_res = audio_close();
	return (result == AUDIO_RES_OK) ? close_res : result;
}

/**
 * @brief 清除停止或上一轮DMA残留的半区通知
 */
static void audio_dma_drain(void) {
	if (g_port == NULL || g_port->sem == NULL) return;
	while (osSemaphoreAcquire(g_port->sem, 0) == osOK);
}

/**
 * @brief 从双缓冲起点启动SAI DMA
 */
static audio_res_t audio_dma_start(void) {
	uint32_t dma_items;
	if (g_port == NULL || g_port->sem == NULL) return AUDIO_RES_INVALID_ARG;
	dma_items = g_port->tx_size * 2u;
	if (dma_items == 0 || dma_items > 0xFFFFu) {
		return AUDIO_RES_DMA_SIZE_INVALID;
	}
	audio_dma_drain();
	g_port->dma_running = true;
	if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t*)g_port->tx[0],
		(uint16_t)dma_items) != HAL_OK) {
		g_port->dma_running = false;
		return AUDIO_RES_DMA_START_FAILED;
	}
	return AUDIO_RES_OK;
}

/**
 * @brief 停止SAI DMA并丢弃所有旧通知
 */
static void audio_dma_stop(void) {
	if (g_port == NULL) return;
	g_port->dma_running = false;
	HAL_SAI_DMAStop(&hsai_BlockA1);
	audio_dma_drain();
}

/**
 * @brief 初始化播放器
 * @param path 文件路径
 * @param meta_only 仅读取元数据
 * @return 统一操作结果
 */
static audio_res_t audio_init(const uint8_t* path, bool meta_only) {
	audio_res_t res;
	uint32_t frames;
	if (path == NULL) return AUDIO_RES_INVALID_ARG;
	audio_port_t* port = (audio_port_t*)pvPortMalloc(sizeof(audio_port_t));
	if (port == NULL) return AUDIO_RES_NO_MEMORY;
	memset(port, 0, sizeof(audio_port_t));
	port->meta_only = meta_only;
	g_port = port;

	// 根据扩展名查找对应格式的解码器
	const char* ext = strrchr((const char*)path, '.');
	if (ext != NULL) {
#if AUDIO_SUPPORT_WAV
		if (strcmp(ext, ".wav") == 0) g_port->dec = &audio_wav_decoder;
#endif /* AUDIO_SUPPORT_WAV */
#if AUDIO_SUPPORT_MP3
		if (strcmp(ext, ".mp3") == 0) g_port->dec = &audio_mp3_decoder;
#endif /* AUDIO_SUPPORT_MP3 */
#if AUDIO_SUPPORT_FLAC
		if (strcmp(ext, ".flac") == 0) g_port->dec = &audio_flac_decoder;
#endif /* AUDIO_SUPPORT_FLAC */
	}

	if (g_port->dec == NULL) {
		audio_port_free();
		return AUDIO_RES_UNSUPPORTED_FORMAT;
	}

	res = g_port->dec->open(path, port_on_meta, g_port, &g_port->dec_ctx);
	if (res != AUDIO_RES_OK) {
		audio_port_free();
		return res;
	}

	res = g_port->dec->get_info(g_port->dec_ctx, &g_port->meta);
	if (res != AUDIO_RES_OK) {
		audio_port_free();
		return res;
	}
	g_port->channels = g_port->meta.channels;
	if (meta_only) return AUDIO_RES_OK;

	if (g_port->channels == 0 || g_port->channels > AUDIO_PLAY_CH) {
		audio_port_free();
		return AUDIO_RES_UNSUPPORTED_CHANNELS;
	}

	res = audio_set_freq(g_port->meta.sample_rate);
	if (res != AUDIO_RES_OK) {
		audio_port_free();
		return res;
	}

	frames = audio_calc_frames(g_port->channels, g_port->meta.sample_rate);
	if (frames == 0) {
		audio_port_free();
		return AUDIO_RES_BUFFER_TOO_SMALL;
	}

	res = audio_buf_alloc(frames);
	if (res != AUDIO_RES_OK) {
        audio_port_free();
		return res;
    }

	return AUDIO_RES_OK;
}

/**
 * @brief 填充SAI输出缓冲
 * @param tx 目标缓冲
 * @param frames_read 实际读取的PCM帧数输出
 * @return 统一操作结果
 */
static audio_res_t audio_fill_buf(uint32_t* tx, uint32_t *frames_read) {
	audio_res_t res;
	uint32_t n = 0;
	uint32_t samples;
	uint32_t i;
	uint32_t gain;

	if (tx == NULL || frames_read == NULL) return AUDIO_RES_INVALID_ARG;
	res = g_port->dec->read(g_port->dec_ctx, g_port->pcm, g_port->frames, &n);
	*frames_read = n;
	if (res != AUDIO_RES_OK && res != AUDIO_RES_EOF) {
		memset(tx, 0, g_port->tx_size * sizeof(uint32_t));
		return res;
	}
	if (n == 0) {
		memset(tx, 0, g_port->tx_size * sizeof(uint32_t));
		return AUDIO_RES_EOF;
	}

	// Q15音量增益, 避免在逐样本热路径中做浮点乘除。
	gain = ((uint32_t)volume * 32768u + 50u) / 100u;
	samples = n * g_port->channels;
	if (g_port->channels == 1) {
		// 单声道直接写入L/R, 不再先写一次后原地扩展。
		for (i = 0; i < n; i++) {
			int32_t s = ((int32_t)g_port->pcm[i] * (int32_t)gain) >> 15;
			uint32_t slot = ((uint32_t)(uint16_t)(int16_t)s) << 16;
			tx[i * 2] = slot;
			tx[i * 2 + 1] = slot;
		}
		samples = n * 2;
	} else {
		for (i = 0; i < samples; i++) {
			int32_t s = ((int32_t)g_port->pcm[i] * (int32_t)gain) >> 15;
			tx[i] = ((uint32_t)(uint16_t)(int16_t)s) << 16;
		}
	}

	// 不足半区补静音
	if (samples < g_port->tx_size) {
		memset(tx + samples, 0, (g_port->tx_size - samples) * sizeof(uint32_t));
	}
	return AUDIO_RES_OK;
}

/**
 * @brief 配置PLL3为SAI提供音频主时钟
 * @param series 采样率系列(44100/48000/192000)
 * @return 统一操作结果
 */
static audio_res_t audio_set_pll3(uint32_t series) {
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
	PeriphClkInitStruct.PLL3.PLL3M = 25;
	PeriphClkInitStruct.PLL3.PLL3Q = 2;
	PeriphClkInitStruct.PLL3.PLL3R = 2;
	PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
	PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;

	if (series == 44100) {
		// 44.1k系(11.025k/22.05k/44.1k): PLL3P = 11.2896MHz, Mckdiv=1/2/4
		PeriphClkInitStruct.PLL3.PLL3N = 316;
		PeriphClkInitStruct.PLL3.PLL3FRACN = 891;
		PeriphClkInitStruct.PLL3.PLL3P = 28;
	} else if (series == 192000) {
		// 192k: PLL3P = 49.152MHz, Mckdiv=1
		PeriphClkInitStruct.PLL3.PLL3N = 344;
		PeriphClkInitStruct.PLL3.PLL3FRACN = 524;
		PeriphClkInitStruct.PLL3.PLL3P = 7;
	} else if (series == 48000) {
		// 48k/96k系(8k/16k/32k/48k/96k): PLL3P = 24.576MHz, Mckdiv=12/6/3/2/1
		PeriphClkInitStruct.PLL3.PLL3N = 344;
		PeriphClkInitStruct.PLL3.PLL3FRACN = 524;
		PeriphClkInitStruct.PLL3.PLL3P = 14;
	}

	PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL3;
	return (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) == HAL_OK) ?
		AUDIO_RES_OK : AUDIO_RES_SAI_CONFIG_FAILED;
}

/**
 * @brief 设置输出采样率
 * @param sample_rate 采样率(Hz)
 * @return 统一操作结果
 */
static audio_res_t audio_set_freq(uint32_t sample_rate) {
	audio_res_t res;
	uint32_t series = 0;

	switch (sample_rate) {
		case SAI_AUDIO_FREQUENCY_8K:
		case SAI_AUDIO_FREQUENCY_16K:
		case SAI_AUDIO_FREQUENCY_32K:
		case SAI_AUDIO_FREQUENCY_48K:
		case SAI_AUDIO_FREQUENCY_96K:
			series = 48000; break;
		case SAI_AUDIO_FREQUENCY_11K:
		case SAI_AUDIO_FREQUENCY_22K:
		case SAI_AUDIO_FREQUENCY_44K:
			series = 44100; break;
		case SAI_AUDIO_FREQUENCY_192K:
			series = 192000; break;
		default: return AUDIO_RES_UNSUPPORTED_SAMPLE_RATE;
	}

	res = audio_set_pll3(series);
	if (res != AUDIO_RES_OK) return res;

	hsai_BlockA1.Init.AudioFrequency = sample_rate;
	return (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD,
		SAI_PROTOCOL_DATASIZE_32BIT, 2) == HAL_OK) ?
		AUDIO_RES_OK : AUDIO_RES_SAI_CONFIG_FAILED;
}

/**
 * @brief 播放音频文件
 * @param path 文件路径
 */
static audio_res_t audio_play(const uint8_t *path) {
	audio_res_t res;
	audio_res_t fill_res[2];
    osEventFlagsSet(System_StatusHandle, APP_NEED_USART);

	res = audio_init(path, false);
	if (res != AUDIO_RES_OK) {
		osEventFlagsClear(System_StatusHandle, APP_NEED_USART);
		return res;
	}

	g_port->sem = osSemaphoreNew(2, 0, NULL);
	if (g_port->sem == NULL) {
		return audio_finish(AUDIO_RES_SEMAPHORE_FAILED);
	}

	// 预填两个半区并启动DMA循环播放
	uint8_t fill_idx = 0;
	fill_res[0] = audio_fill_buf(g_port->tx[0], &g_port->valid_frames[0]);
	if (fill_res[0] != AUDIO_RES_OK && fill_res[0] != AUDIO_RES_EOF &&
		fill_res[0] != AUDIO_RES_DECODE_FAILED) return audio_finish(fill_res[0]);
	fill_res[1] = audio_fill_buf(g_port->tx[1], &g_port->valid_frames[1]);
	if (fill_res[1] != AUDIO_RES_OK && fill_res[1] != AUDIO_RES_EOF &&
		fill_res[1] != AUDIO_RES_DECODE_FAILED) return audio_finish(fill_res[1]);
	if (fill_res[0] == AUDIO_RES_EOF && fill_res[1] == AUDIO_RES_EOF) {
		return audio_finish(AUDIO_RES_EMPTY_STREAM);
	}

	const uint64_t total_frames = g_port->meta.total_frames;
	const uint32_t seek_step = g_port->meta.sample_rate * AUDIO_SEEK_STEP;
	uint32_t play_frames = 0;                          // 已播放帧数
	int64_t seek_frame = -1;                           // 待跳转帧索引(-1无请求)
	uint32_t last_disp = osKernelGetTickCount();       // 上次刷新进度时间(元数据区已由tui_init输出)
	bool abort = false;
	bool paused = false;
	bool pause_pending = false;
	char key;

	// 复位当前时间
	g_port->cur_time.base_s = 0;
	g_port->cur_time.base_100ms = 0;

	/* 界面首次绘制可能较慢，必须在DMA启动前完成，避免刚开播就积压两个半区通知。 */
	tui_init(&g_port->meta, volume);
	res = audio_dma_start();
	if (res != AUDIO_RES_OK) return audio_finish(res);

	while (1) {
		// 按键处理: ^C停止, 空格暂停/恢复, 方向上/下音量, 左/右进度
		while (shell.read(&key, 1) > 0) {
			if (key == 0x03) {
				abort = true;
				break;
			}
			if (key == ' ') {
				if (paused) {
					if (HAL_SAI_DMAResume(&hsai_BlockA1) != HAL_OK) {
						res = AUDIO_RES_DMA_CONTROL_FAILED;
						abort = true;
						break;
					}
					paused = false;
				} else {
					/* 真正暂停放到下一个半区边界，避免截断声道或采样字。 */
					pause_pending = !pause_pending;
				}
				last_disp = 0;
				continue;
			}
			if (key == 0x1B) {
				char seq[2];
				if (shell.read(&seq[0], 1) > 0 && seq[0] == '[' && shell.read(&seq[1], 1) > 0) {
					if (seq[1] == 'A') volume = (volume >= 100) ? 100 : volume + 1;    // 上: 音量+
					else if (seq[1] == 'B') volume = (volume <= 0) ? 0 : volume - 1;   // 下: 音量-
					else if (seq[1] == 'C') seek_frame = (seek_frame >= 0) ?                // 右: 快进(连续按键累加)
						seek_frame + seek_step : play_frames + seek_step;
					else if (seq[1] == 'D') seek_frame = (seek_frame >= 0) ?                // 左: 快退(连续按键累减)
						seek_frame - seek_step : (int64_t)play_frames - seek_step;
					else continue;
					last_disp = 0;   // 按键后立即刷新进度条
				}
			}
		}
		if (abort) {
			if (res != AUDIO_RES_DMA_CONTROL_FAILED) res = AUDIO_RES_ABORTED;
			break;
		}

		// 执行进度跳转: 先停止SAI发送, seek完成后再重启
		if (seek_frame < -1) seek_frame = 0;
		if (seek_frame >= 0) {
			uint32_t target = (uint32_t)seek_frame;
			if (target >= total_frames) target = (total_frames > 0) ? (uint32_t)total_frames - 1 : 0;

			// 停止SAI发送, 避免seek期间继续播放旧缓冲
			audio_dma_stop();

			res = g_port->dec->seek(g_port->dec_ctx, target);
			if (res == AUDIO_RES_OK) {
				play_frames = target;
				// 更新当前时间
				{
					uint32_t ms = (g_port->meta.sample_rate > 0) ?
						(uint32_t)((uint64_t)target * 1000 / g_port->meta.sample_rate) : 0;
					g_port->cur_time.base_s = (uint16_t)(ms / 1000);
					g_port->cur_time.base_100ms = (uint8_t)((ms % 1000) / 10);
				}
			}

			// seek失败时沿用解码器当前可读位置，保持旧版的非致命容错行为。
			// 无论seek是否成功，都重新预填两个半区并重启DMA。
			fill_idx = 0;
			fill_res[0] = audio_fill_buf(g_port->tx[0], &g_port->valid_frames[0]);
			if (fill_res[0] != AUDIO_RES_OK && fill_res[0] != AUDIO_RES_EOF &&
				fill_res[0] != AUDIO_RES_DECODE_FAILED) { res = fill_res[0]; break; }
			fill_res[1] = audio_fill_buf(g_port->tx[1], &g_port->valid_frames[1]);
			if (fill_res[1] != AUDIO_RES_OK && fill_res[1] != AUDIO_RES_EOF &&
				fill_res[1] != AUDIO_RES_DECODE_FAILED) { res = fill_res[1]; break; }
			if (fill_res[0] == AUDIO_RES_EOF && fill_res[1] == AUDIO_RES_EOF) {
				res = AUDIO_RES_OK;
				break;
			}
			res = audio_dma_start();
			if (res != AUDIO_RES_OK) break;
			if (paused && HAL_SAI_DMAPause(&hsai_BlockA1) != HAL_OK) {
				res = AUDIO_RES_DMA_CONTROL_FAILED;
				break;
			}
			seek_frame = -1;
		}

		/* 暂停期间仅轮询控制键，不消耗DMA通知，也不推进播放时间。 */
		if (paused) {
			osDelay(10);
			continue;
		}

		// 等待DMA半区播放完毕(2s超时防卡死)
		if (osSemaphoreAcquire(g_port->sem, 2000) != osOK) {
			res = AUDIO_RES_DMA_TIMEOUT;
			break;
		}
		if (pause_pending) {
			if (HAL_SAI_DMAPause(&hsai_BlockA1) != HAL_OK) {
				res = AUDIO_RES_DMA_CONTROL_FAILED;
				break;
			}
			pause_pending = false;
			paused = true;
		}

		/*
		 * 又有一个通知已排队，说明DMA已经跨过两个半区并开始回卷。
		 * 此时fill_idx已不再代表安全写入区，立即停机并从双缓冲边界重同步，
		 * 防止持续重复旧片段或在DMA读取时改写缓冲造成左右声道错乱。
		 */
		if (osSemaphoreGetCount(g_port->sem) > 0) {
			play_frames += g_port->valid_frames[0] + g_port->valid_frames[1];
			audio_dma_stop();
			fill_idx = 0;
			fill_res[0] = audio_fill_buf(g_port->tx[0], &g_port->valid_frames[0]);
			if (fill_res[0] != AUDIO_RES_OK && fill_res[0] != AUDIO_RES_EOF &&
				fill_res[0] != AUDIO_RES_DECODE_FAILED) { res = fill_res[0]; break; }
			fill_res[1] = audio_fill_buf(g_port->tx[1], &g_port->valid_frames[1]);
			if (fill_res[1] != AUDIO_RES_OK && fill_res[1] != AUDIO_RES_EOF &&
				fill_res[1] != AUDIO_RES_DECODE_FAILED) { res = fill_res[1]; break; }
			if (fill_res[0] == AUDIO_RES_EOF && fill_res[1] == AUDIO_RES_EOF) {
				res = AUDIO_RES_OK;
				break;
			}
			res = audio_dma_start();
			if (res != AUDIO_RES_OK) break;
			if (paused && HAL_SAI_DMAPause(&hsai_BlockA1) != HAL_OK) {
				res = AUDIO_RES_DMA_CONTROL_FAILED;
				break;
			}
			continue;
		}
		play_frames += g_port->valid_frames[fill_idx];

		// 更新当前时间
		{
			uint32_t ms = (g_port->meta.sample_rate > 0) ?
					(uint32_t)((uint64_t)play_frames * 1000 / g_port->meta.sample_rate) : 0;
			g_port->cur_time.base_s = (uint16_t)(ms / 1000);
			g_port->cur_time.base_100ms = (uint8_t)((ms % 1000) / 10);
		}

		res = audio_fill_buf(g_port->tx[fill_idx], &g_port->valid_frames[fill_idx]);
		if (res != AUDIO_RES_OK && res != AUDIO_RES_EOF &&
			res != AUDIO_RES_DECODE_FAILED) break;
		// 两个半区都无有效数据时, 最后一帧已播放完毕。
		if (res == AUDIO_RES_EOF &&
			g_port->valid_frames[0] == 0 && g_port->valid_frames[1] == 0) {
			res = AUDIO_RES_OK;
			break;
		}
		fill_idx = 1 - fill_idx;

		// 定时刷新播放器界面
		if (osKernelGetTickCount() - last_disp >= 150) {
			last_disp = osKernelGetTickCount();
			tui_update(&g_port->cur_time, volume);
		}
	}

	return audio_finish(res);
}

/**
 * @brief 显示音频元数据
 * @param path 文件路径
 */
static audio_res_t audio_info(const uint8_t* path) {
	audio_res_t res = audio_init(path, true);
	if (res != AUDIO_RES_OK) return res;

    logPrintln("Sample Rate: %uHz", g_port->meta.sample_rate);
	logPrintln("Bit Depth: %u", g_port->meta.bits_per_sample);
	logPrintln("Channels: %u", g_port->meta.channels);
	logPrintln("Duration: %um%us", g_port->meta.duration_ms / 60000, (g_port->meta.duration_ms / 1000) % 60);

	return audio_port_free();
}

/**
 * @brief 显示audio工具帮助信息
 */
static void audio_help(void) {
	logPrintln(
		"Usage: audio [OPTION] FILE\r\n"
		"  -h, --help       show this help\r\n"
		"  -i               show audio file metadata\r\n"
		"  FILE             play audio file\r\n"
		"Controls: Space pause/resume, arrows seek/volume, Ctrl+C stop"
	);
}

/**
 * @brief audio工具主函数，解析指令并分发
 */
static void audio(int argc, char *argv[]) {
	audio_res_t res;
	uint8_t i;
	uint8_t k;

	if (argc < 2) {
		audio_help();
		return;
	}

	// 通用参数解析：区分选项和位置参数(文件)
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			// 选项参数
			if (argv[i][1] == '-') {
				// 长选项
				if (strcmp(argv[i], "--help") == 0) {
					audio_help(); return;
				}
				logPrintln("invalid option -- '%s'", argv[i]);
				audio_help(); return;
			}
			// 短选项
			for (k = 1; argv[i][k] != '\0'; k++) {
				switch (argv[i][k]) {
					case 'h': audio_help(); return;
                    case 'i':
						// -i后跟音频文件
						if (i + 2 != argc) {
							logPrintln("Usage: audio -i <path>");
							return;
						}
						res = audio_info((uint8_t *)argv[i + 1]);
						if (res != AUDIO_RES_OK) logPrintln("\r\n%s", audio_res_str(res));
                        return;
					default:
						logPrintln("invalid option -- '%c'", argv[i][k]);
						audio_help(); return;
				}
			}
		} else {
			// 位置参数，视为播放文件
			if (argc != 2) {
				logPrintln("Usage: audio <path>");
				return;
			}
			res = audio_play((uint8_t *)argv[i]);
			if (res != AUDIO_RES_OK && res != AUDIO_RES_ABORTED) {
				logPrintln("\r\n%s", audio_res_str(res));
			}
			return;
		}
	}
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
audio, audio, Audio Player);

/**
 * @brief SAI DMA半区传输完成回调
 * @param hsai SAI句柄
 */
void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef* hsai) {
	if (hsai->Instance == SAI1_Block_A && g_port != NULL &&
		g_port->sem != NULL && g_port->dma_running) {
		osSemaphoreRelease(g_port->sem);
	}
}

/**
 * @brief SAI DMA整区传输完成回调
 * @param hsai SAI句柄
 */
void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef* hsai) {
	if (hsai->Instance == SAI1_Block_A && g_port != NULL &&
		g_port->sem != NULL && g_port->dma_running) {
		osSemaphoreRelease(g_port->sem);
	}
}
